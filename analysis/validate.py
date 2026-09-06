"""Independent validation of the engine against buy-and-hold SPY.

The engine is only worth trusting on a strategy whose answer is unknown if it
first reproduces one whose answer is known. This script rebuilds the same
buy-and-hold position in pandas, straight from the CSV, and compares both the
equity path and the summary statistics against what the C++ engine produced.

The reference implementation here shares no code with the engine. It is
deliberately written from the price series rather than from the engine output,
so that an accounting bug shows up as a disagreement instead of being copied
into both sides.

    ./build/bin/validate_buy_and_hold
    python analysis/validate.py

Conventions, matching src/summary.cpp exactly:
    daily return    r_t   = equity_t / equity_{t-1} - 1
    CAGR                  = (equity_n / equity_0) ** (1 / years) - 1,
                            years = n_returns / 252
    volatility            = stdev(r, ddof=1) * sqrt(252)
    Sharpe                = mean(r - 0.02/252) / stdev(r, ddof=1) * sqrt(252)
    max drawdown          = max(1 - equity / cummax(equity))
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd

TRADING_DAYS = 252.0
RISK_FREE = 0.02
INITIAL_CAPITAL = 1_000_000.0

# Tolerances. The equity path should agree to floating-point noise; the
# summary statistics are held to the 1e-6 the spec asks for.
PATH_TOLERANCE = 1e-6
STAT_TOLERANCE = 1e-6


def reference_equity(prices: pd.DataFrame) -> pd.Series:
    """Buy-and-hold equity, built independently of the engine.

    The convention being reproduced: the strategy signals on the first bar
    close, the portfolio sizes the order at that close, and the execution
    handler fills at the *next* bar open. So no position is held on day 0, and
    the entry price is the day 1 open -- not the day 0 close that generated
    the signal, and not the day 0 open.
    """
    close = prices["close"].to_numpy(dtype=float)
    open_ = prices["open"].to_numpy(dtype=float)

    quantity = INITIAL_CAPITAL / close[0]
    cash = INITIAL_CAPITAL - quantity * open_[1]

    equity = np.empty(len(close), dtype=float)
    equity[0] = INITIAL_CAPITAL
    equity[1:] = cash + quantity * close[1:]
    return pd.Series(equity, index=prices.index, name="equity")


def summarise(equity: pd.Series) -> dict[str, float]:
    returns = equity.to_numpy(dtype=float)
    daily = returns[1:] / returns[:-1] - 1.0

    years = len(daily) / TRADING_DAYS
    stdev = float(np.std(daily, ddof=1))
    peak = np.maximum.accumulate(returns)

    return {
        "annualised_return": float((returns[-1] / returns[0]) ** (1.0 / years) - 1.0),
        "annualised_volatility": stdev * np.sqrt(TRADING_DAYS),
        "sharpe": float(np.mean(daily - RISK_FREE / TRADING_DAYS))
        / stdev
        * np.sqrt(TRADING_DAYS),
        "max_drawdown": float(np.max(1.0 - returns / peak)),
        "observations": float(len(daily)),
    }


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Validate the engine against buy-and-hold SPY")
    parser.add_argument("--prices", type=Path, default=root / "data" / "SPY.csv")
    parser.add_argument(
        "--engine", type=Path, default=root / "results" / "buy_and_hold_equity.csv"
    )
    args = parser.parse_args()

    if not args.prices.exists():
        print(f"missing price data: {args.prices}\nrun: python data/fetch_data.py", file=sys.stderr)
        return 2
    if not args.engine.exists():
        print(
            f"missing engine output: {args.engine}\nrun: ./build/bin/validate_buy_and_hold",
            file=sys.stderr,
        )
        return 2

    prices = pd.read_csv(args.prices, parse_dates=["date"], index_col="date")
    engine = pd.read_csv(args.engine, parse_dates=["date"], index_col="date")["equity"]

    reference = reference_equity(prices)

    failures: list[str] = []

    # 1. The two equity paths must be the same series over the same dates.
    if len(reference) != len(engine):
        failures.append(f"length mismatch: engine {len(engine)}, reference {len(reference)}")
    elif not reference.index.equals(engine.index):
        first = next(
            (i for i, (a, b) in enumerate(zip(reference.index, engine.index)) if a != b), None
        )
        failures.append(f"date mismatch at row {first}")
    else:
        relative = (engine - reference).abs() / reference.abs()
        worst = float(relative.max())
        print(f"equity path      max relative difference {worst:.3e} over {len(engine)} bars")
        if worst > PATH_TOLERANCE:
            worst_date = relative.idxmax()
            failures.append(
                f"equity path differs by {worst:.3e} at {worst_date.date()}: "
                f"engine {engine.loc[worst_date]:.6f} vs reference {reference.loc[worst_date]:.6f}"
            )

    # 2. The summary statistics must agree to 1e-6.
    engine_stats = summarise(engine)
    reference_stats = summarise(reference)

    print()
    print(f"{'metric':<24}{'engine':>16}{'pandas':>16}{'difference':>14}")
    for key in ("annualised_return", "annualised_volatility", "sharpe", "max_drawdown"):
        difference = abs(engine_stats[key] - reference_stats[key])
        print(f"{key:<24}{engine_stats[key]:>16.10f}{reference_stats[key]:>16.10f}{difference:>14.2e}")
        if difference > STAT_TOLERANCE:
            failures.append(f"{key} differs by {difference:.3e}")

    # 3. Sanity checks against what is known about SPY from 2007: the
    #    financial crisis has to be in there.
    print()
    checks = [
        ("sharpe in [0.4, 0.6]", 0.4 <= reference_stats["sharpe"] <= 0.6),
        ("max drawdown in [0.50, 0.60]", 0.50 <= reference_stats["max_drawdown"] <= 0.60),
        ("annualised volatility in [0.15, 0.25]", 0.15 <= reference_stats["annualised_volatility"] <= 0.25),
    ]
    for label, passed in checks:
        print(f"  [{'ok' if passed else 'FAIL'}] {label}")
        if not passed:
            failures.append(label)

    print()
    if failures:
        print("VALIDATION FAILED", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print("VALIDATION PASSED: engine agrees with the independent pandas computation")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
