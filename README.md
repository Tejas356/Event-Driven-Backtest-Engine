# tsmom-engine

An event-driven backtesting engine in C++20, built so that look-ahead bias is
structurally impossible rather than merely avoided by careful coding. A time-series
momentum strategy runs on top of it as a validation case.

---

## The design claim

Most backtests are vectorised: load the full price series into a dataframe, compute a
signal over it, shift by one, multiply by returns. It's fast to write, and it makes
look-ahead bias a matter of discipline. One misaligned shift and the results are
silently wrong in the direction that flatters them.

This engine takes the other approach. The strategy object is only ever *handed* data
timestamped at or before the current simulation time. There is no method anywhere on
the data handler that returns the full series — not for convenience, not for testing.
The strategy cannot see the future because it is never given the future.

That claim is enforced by a test (`tests/test_lookahead.cpp`) which iterates the entire
dataset and asserts, on every call, that no bar returned to the strategy carries a
timestamp later than the current simulation time.

Two secondary properties follow from the same architecture:

- **Signal lag is physical, not conventional.** Signals computed at bar *t*'s close are
  filled at bar *t+1*'s open by the execution handler. There is no code path that fills
  at the price which generated the signal.
- **Costs are charged on every unit of turnover**, and gross and net results are reported
  side by side so the cost drag is visible rather than buried in a summary statistic.

---

## Why C++

An event loop is inherently sequential — bar *N+1*'s processing depends on the state
after bar *N* — so there is nothing to vectorise and NumPy offers no help. This is a
genuine case where a compiled language earns its place, as distinct from research code
where the bottleneck is iteration speed rather than execution speed.

Rolling statistics update incrementally in O(1): Welford's algorithm for variance, EWMA
for volatility. No window is ever recomputed from scratch.

The peripheral scripts (data fetching, plotting) are Python, because that is the correct
place for the boundary.

---

## Architecture

```
                 ┌──────────────┐
                 │ DataHandler  │  bounded lookback only
                 └──────┬───────┘
                        │ MarketEvent
                        ▼
    ┌───────────────────────────────────────┐
    │            EventQueue                 │
    └───────────────────────────────────────┘
         │            │            │
    MarketEvent  SignalEvent  OrderEvent/FillEvent
         ▼            ▼            ▼
    ┌─────────┐  ┌──────────┐  ┌─────────────────┐
    │Strategy │  │Portfolio │  │ExecutionHandler │
    └─────────┘  └──────────┘  └─────────────────┘
```

The outer loop advances simulation time. The inner loop drains every event occurring
*at* that time before advancing. Strategies emit target weights; the portfolio owns
sizing and order generation; the execution handler owns fills and costs. Swapping in a
more realistic cost model touches one class.

```
include/backtest/     engine, events, data handler, portfolio, execution, statistics, metrics
strategies/           buy-and-hold (validation), tsmom
apps/                 backtest runner, parameter sweep
tests/                unit tests, including the look-ahead test
analysis/             python plotting and independent validation
data/                 fetch script (CSVs are gitignored)
```

---

## Strategy

Time-series momentum, following Moskowitz, Ooi and Pedersen (2012). Long an asset if its
trailing 12-month return is positive, short if negative. Positions sized inversely to
each asset's EWMA volatility so each contributes equal risk, with a gross leverage cap.
Rebalanced monthly.

Universe: eight liquid ETFs spanning US and international equity, Treasuries, gold,
broad commodities and the dollar. Cross-asset diversity is what makes trend following
work; a basket of correlated equity ETFs is not diversification.

The strategy is arithmetically trivial — a mean, a sign and a division. It is here
because it has the strongest out-of-sample evidence of anything in the public
literature, which makes it a reasonable test of whether the engine produces sane
numbers. **The engine is the deliverable.**

---

## Build

Requires CMake ≥3.20 and a C++20 compiler. GoogleTest is fetched automatically.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Fetch data and run:

```bash
pip install yfinance pandas
python data/fetch_data.py
./build/apps/run_backtest --config config/default.toml
python analysis/analyse.py
```

---

## Validation

Before any strategy result is reported, the harness is checked against a case with a
known answer. Buy-and-hold SPY from 2007 is run through the full engine and its
annualised return and volatility are compared against the same figures computed
independently in pandas, agreeing to 1e-6. The equity curve reproduces the 2008–09
drawdown of roughly 55%.

An engine that cannot reproduce buy-and-hold cannot be trusted on anything harder.

---

## Results

<!-- Fill in after Step 12. Replace the placeholders; do not tune parameters to
     improve these numbers before writing them down. -->

| Metric | Gross | Net |
|---|---|---|
| Annualised return | — | — |
| Annualised volatility | — | — |
| Sharpe | — | — |
| Max drawdown | — | — |
| Drawdown duration (days) | — | — |
| Skew | — | — |
| Annualised turnover | — | — |

Robustness across lookback windows of 63 / 126 / 189 / 252 / 378 trading days:

<!-- Report the plateau, or state plainly that there isn't one. A result that holds
     across neighbouring parameter values is evidence; a spike at one value is not. -->

---

## Limitations

Stated plainly, because a backtest that doesn't declare what it omits isn't worth
reading.

- **ETF proxies, not futures.** Real trend followers trade futures, which have different
  cost, margin and roll characteristics. The ETF universe is a convenience.
- **No borrow costs or shorting constraints.** Short positions are assumed freely
  available at no financing cost, which is false.
- **Flat basis-point cost assumption.** No market impact model, so results do not
  reflect what happens as position size grows relative to volume.
- **Single-path backtest.** No bootstrap or Monte Carlo confidence interval on the
  Sharpe ratio. Given the standard error of a Sharpe estimate over this sample length,
  the reported figure is compatible with a fairly wide range of true values.
- **One sample, one regime history.** Trend following performed poorly from roughly
  2010–2019 and strongly in 2022. A backtest over this window contains both, but that is
  one path, not a distribution.
- **No survivorship correction is needed here** (the ETF universe is fixed and
  long-lived), but this would be a material issue if the universe were extended to
  individual equities.

---

## Not a trading system

This is a research and engineering exercise. It is not investment advice, it has never
traded live, and the results should not be taken as evidence that the strategy would be
profitable after the frictions listed above. Anyone reading this as a reason to trade
should read the limitations section again.

---

## References

- Moskowitz, Ooi and Pedersen (2012), *Time Series Momentum*, Journal of Financial
  Economics.
- Welford (1962), *Note on a Method for Calculating Corrected Sums of Squares and
  Products*, Technometrics.

## Licence

MIT.
