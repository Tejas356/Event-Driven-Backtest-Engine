"""Plots the results of a backtest run.

    ./build/bin/run_backtest --config=config/default.toml
    ./build/bin/sweep --config=config/default.toml
    python analysis/analyse.py

Produces, in results/:
    equity_curve.png      log-scale equity, gross beside net
    drawdown.png          underwater plot
    rolling_sharpe.png    rolling 12-month Sharpe
    cost_drag.png         gross minus net cumulative return
    sweep_heatmap.png     Sharpe across the parameter grid

Log scale on the equity plot is deliberate: on a linear axis a 19-year curve
makes the last few years look like all the risk, because equal vertical
distances stop meaning equal returns.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

TRADING_DAYS = 252

# One accent for net, a muted tone for gross, and a single warm colour for
# anything that represents a loss. Kept small on purpose: a chart with five
# colours and three of them meaning nothing is harder to read, not richer.
NET = "#1f4e79"
GROSS = "#8fa9c4"
LOSS = "#a63a2b"
GRID = "#dcdcdc"


def style(ax, title: str, ylabel: str) -> None:
    ax.set_title(title, fontsize=11, loc="left", pad=10)
    ax.set_ylabel(ylabel, fontsize=9)
    ax.grid(True, color=GRID, linewidth=0.6)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    ax.tick_params(labelsize=8)


def plot_equity(equity: pd.DataFrame, out: Path) -> None:
    fig, ax = plt.subplots(figsize=(9, 4.5))
    ax.plot(equity.index, equity["gross_equity"], color=GROSS, linewidth=1.2, label="gross")
    ax.plot(equity.index, equity["equity"], color=NET, linewidth=1.4, label="net of costs")
    ax.set_yscale("log")
    style(ax, "Equity curve (log scale)", "equity")
    ax.legend(frameon=False, fontsize=8)
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    plt.close(fig)


def plot_drawdown(equity: pd.DataFrame, out: Path) -> None:
    curve = equity["equity"]
    drawdown = curve / curve.cummax() - 1.0

    fig, ax = plt.subplots(figsize=(9, 3.2))
    ax.fill_between(drawdown.index, drawdown.to_numpy() * 100.0, 0.0, color=LOSS, alpha=0.35)
    ax.plot(drawdown.index, drawdown.to_numpy() * 100.0, color=LOSS, linewidth=0.9)
    worst = drawdown.idxmin()
    ax.annotate(
        f"{drawdown.min() * 100:.1f}%  {worst.date()}",
        xy=(worst, drawdown.min() * 100.0),
        xytext=(6, -10),
        textcoords="offset points",
        fontsize=8,
        color=LOSS,
    )
    style(ax, "Drawdown", "%")
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    plt.close(fig)


def plot_rolling_sharpe(equity: pd.DataFrame, out: Path, risk_free: float) -> None:
    returns = equity["equity"].pct_change().dropna()
    window = TRADING_DAYS
    excess = returns - risk_free / TRADING_DAYS
    rolling = (
        excess.rolling(window).mean() / returns.rolling(window).std(ddof=1) * np.sqrt(TRADING_DAYS)
    ).dropna()

    fig, ax = plt.subplots(figsize=(9, 3.2))
    ax.axhline(0.0, color="#666666", linewidth=0.8)
    ax.plot(rolling.index, rolling.to_numpy(), color=NET, linewidth=1.0)
    style(ax, "Rolling 12-month Sharpe", "Sharpe")
    # The spread here is the point: a single headline Sharpe hides how much
    # of the sample it was nowhere near.
    ax.annotate(
        f"range {rolling.min():.2f} to {rolling.max():.2f}",
        xy=(0.99, 0.04),
        xycoords="axes fraction",
        ha="right",
        fontsize=8,
        color="#555555",
    )
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    plt.close(fig)


def plot_cost_drag(equity: pd.DataFrame, out: Path) -> None:
    gross = equity["gross_equity"] / equity["gross_equity"].iloc[0] - 1.0
    net = equity["equity"] / equity["equity"].iloc[0] - 1.0

    fig, ax = plt.subplots(figsize=(9, 3.4))
    ax.plot(gross.index, gross.to_numpy() * 100.0, color=GROSS, linewidth=1.2, label="gross")
    ax.plot(net.index, net.to_numpy() * 100.0, color=NET, linewidth=1.4, label="net")
    ax.fill_between(gross.index, net.to_numpy() * 100.0, gross.to_numpy() * 100.0,
                    color=LOSS, alpha=0.20, label="cost drag")
    style(ax, "Cumulative return, gross against net", "%")
    ax.legend(frameon=False, fontsize=8)
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    plt.close(fig)


def plot_sweep(sweep: pd.DataFrame, out: Path) -> None:
    # Sharpe with the risk-free deduction removed, matching what the sweep
    # prints; see the note in NOTES.md about idle cash.
    grid = sweep.pivot(index="lookback", columns="volatility_target", values="net_sharpe_no_rf")

    fig, ax = plt.subplots(figsize=(6.5, 4.2))
    # A sequential scale, not a diverging one: every value here is positive,
    # and a diverging map would invent a midpoint that means nothing.
    image = ax.imshow(grid.to_numpy(), cmap="Blues", aspect="auto", origin="lower")

    ax.set_xticks(range(len(grid.columns)), [f"{c:.2f}" for c in grid.columns])
    ax.set_yticks(range(len(grid.index)), [str(i) for i in grid.index])
    ax.set_xlabel("volatility target", fontsize=9)
    ax.set_ylabel("lookback (trading days)", fontsize=9)

    # Print the value in every cell. A heatmap that needs its own colourbar
    # decoded to compare two cells is decoration, not evidence.
    values = grid.to_numpy()
    midpoint = (values.max() + values.min()) / 2.0
    for row in range(values.shape[0]):
        for col in range(values.shape[1]):
            ax.text(col, row, f"{values[row, col]:.2f}", ha="center", va="center", fontsize=8,
                    color="white" if values[row, col] > midpoint else "#20303f")

    ax.set_title("Net Sharpe across the parameter grid", fontsize=11, loc="left", pad=10)
    fig.colorbar(image, ax=ax, shrink=0.85, label="net Sharpe")
    ax.tick_params(labelsize=8)
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    plt.close(fig)


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Plot backtest results")
    parser.add_argument("--results", type=Path, default=root / "results")
    parser.add_argument("--risk-free", type=float, default=0.02)
    args = parser.parse_args()

    equity_file = args.results / "equity_curve.csv"
    if not equity_file.exists():
        print(
            f"missing {equity_file}\nrun: ./build/bin/run_backtest --config=config/default.toml",
            file=sys.stderr,
        )
        return 2

    equity = pd.read_csv(equity_file, parse_dates=["date"], index_col="date")

    plot_equity(equity, args.results / "equity_curve.png")
    plot_drawdown(equity, args.results / "drawdown.png")
    plot_rolling_sharpe(equity, args.results / "rolling_sharpe.png", args.risk_free)
    plot_cost_drag(equity, args.results / "cost_drag.png")
    written = ["equity_curve.png", "drawdown.png", "rolling_sharpe.png", "cost_drag.png"]

    sweep_file = args.results / "sweep.csv"
    if sweep_file.exists():
        plot_sweep(pd.read_csv(sweep_file), args.results / "sweep_heatmap.png")
        written.append("sweep_heatmap.png")
    else:
        print(f"note: no {sweep_file}, skipping the heatmap (run ./build/bin/sweep)")

    # A short summary next to the plots, so the numbers behind them are
    # visible without opening the JSON.
    curve = equity["equity"]
    returns = curve.pct_change().dropna()
    years = len(returns) / TRADING_DAYS
    drawdown = curve / curve.cummax() - 1.0

    print(f"\n  bars                  {len(curve)}")
    print(f"  years                 {years:.2f}")
    print(f"  annualised return     {(curve.iloc[-1] / curve.iloc[0]) ** (1 / years) - 1:.4f}")
    print(f"  annualised volatility {returns.std(ddof=1) * np.sqrt(TRADING_DAYS):.4f}")
    print(f"  max drawdown          {drawdown.min():.4f}")
    print(f"  cost drag (final)     "
          f"{equity['gross_equity'].iloc[-1] - curve.iloc[-1]:.2f}")
    print("\n  wrote " + ", ".join(written) + f" to {args.results}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
