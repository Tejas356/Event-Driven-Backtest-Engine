"""Download daily ETF bars for the backtest.

Writes one CSV per symbol as date,open,high,low,close,volume, ascending, with
no missing rows. Prices are split- and dividend-adjusted: using raw closes
would put a return step at every distribution, which for a Treasury or gold
ETF is a large part of the total return.

    pip install yfinance pandas
    python data/fetch_data.py
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Eight liquid ETFs spanning asset classes. Cross-asset diversity is what
# makes trend following work; a basket of correlated equity ETFs is not
# diversification. DBC and UUP both launched in 2006-07, which sets the start.
UNIVERSE = {
    "SPY": "US equity",
    "EFA": "Developed international equity",
    "EEM": "Emerging market equity",
    "IEF": "US 7-10y Treasuries",
    "TLT": "US 20y+ Treasuries",
    "GLD": "Gold",
    "DBC": "Broad commodities",
    "UUP": "US dollar index",
}

START = "2007-01-01"


def fetch(symbol: str, start: str, end: str | None) -> "pd.DataFrame":
    import yfinance as yf

    # auto_adjust applies split and dividend adjustment to all OHLC fields, so
    # open and close stay on the same basis. Adjusting close alone would make
    # the fill price and the signal price inconsistent.
    frame = yf.download(
        symbol,
        start=start,
        end=end,
        auto_adjust=True,
        progress=False,
        multi_level_index=False,
    )
    if frame is None or frame.empty:
        raise RuntimeError(f"no data returned for {symbol}")

    frame = frame.rename(columns=str.lower)
    frame = frame[["open", "high", "low", "close", "volume"]]
    frame = frame.dropna()
    frame = frame[(frame[["open", "high", "low", "close"]] > 0).all(axis=1)]
    frame = frame.sort_index()
    frame.index.name = "date"
    return frame


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--start", default=START)
    parser.add_argument("--end", default=None)
    parser.add_argument("--symbols", nargs="*", default=sorted(UNIVERSE))
    parser.add_argument("--out", type=Path, default=Path(__file__).parent)
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)

    failures = []
    for symbol in args.symbols:
        try:
            frame = fetch(symbol, args.start, args.end)
        except Exception as error:  # noqa: BLE001 - report and continue
            print(f"{symbol}: FAILED ({error})", file=sys.stderr)
            failures.append(symbol)
            continue

        path = args.out / f"{symbol}.csv"
        frame.to_csv(path, float_format="%.10g", date_format="%Y-%m-%d")
        print(
            f"{symbol}: {len(frame)} bars, "
            f"{frame.index[0].date()} to {frame.index[-1].date()} -> {path.name}"
        )

    if failures:
        print(f"\nfailed: {', '.join(failures)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
