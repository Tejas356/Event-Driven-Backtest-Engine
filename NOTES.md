# Research log

Every parameter variation tried, and what it produced. The count matters: if a
configuration eventually looks good, the number of variations tried is what
determines how much to discount it. Kept from the start rather than
reconstructed afterwards, because reconstructed logs are always flattering.

Data: eight ETFs, daily adjusted OHLC, 2007-01-03 to 2026-09-04 (4950 bars;
UUP starts 2007-03-01 with 4911). Downloaded 2026-09-06 via yfinance with
auto_adjust=True.

---

## Step 7 -- harness validation (baseline, not a parameter choice)

Buy-and-hold SPY, 1,000,000 initial capital, costs switched off, entry at the
second bar open.

| Metric | Engine | pandas (independent) |
|---|---|---|
| Annualised return | 0.1101949498 | 0.1101949498 |
| Annualised volatility | 0.1955929143 | 0.1955929143 |
| Sharpe (rf 2%) | 0.5301683452 | 0.5301683452 |
| Max drawdown | 0.5514074306 | 0.5514074306 |

Equity path agrees to a maximum relative difference of 2.6e-16 over 4950
bars. Deepest drawdown troughs in March 2009.

All four figures sit inside the ranges the spec calls for (Sharpe 0.4-0.6,
drawdown around 55%), so the harness is trusted from here.

No parameters were varied. The costless run is deliberate: it isolates the
accounting from the cost model.

---

## Step 10 -- inverse-volatility sizing

Config: lookback 252, EWMA halflife 60 (seed 20), volatility target 10% per
position, gross cap 2.0, deadband 5bp of equity, **daily** rebalancing.
Monthly is Step 11; daily is recorded here as the comparison.

| | No costs | 2bp round trip |
|---|---|---|
| Annualised return | 1.75% | 1.64% |
| Annualised volatility | 4.76% | 4.76% |
| Net Sharpe | -0.031 | -0.054 |
| Max drawdown | 11.1% (1173 days) | 11.2% (1180 days) |
| Annualised turnover | 1087% | 1087% |
| Mean / max gross exposure | 0.79 / 1.33 | 0.79 / 1.33 |

Realised volatility of 4.76% against a diversified target of
10%/sqrt(8) = 3.54%: within the 3 percentage points allowed, and above it in
the direction the positive correlation between trend positions predicts.

Turnover of 1087% a year is the thing to notice. Daily rebalancing churns the
book on volatility-estimate wiggles that carry no signal. Step 11 is the fix.

Sharpe is slightly negative here. Not tuned away -- this is what daily
rebalancing produces, and it is recorded rather than quietly dropped.

### Bug found and fixed: stale prices at signal time

The first version raised all eight signals from `on_market`, on the first
market event of each day. Market events arrive one symbol at a time, so seven
of the eight were being sized against yesterday closes and a stale equity.

Consequences, before the fix:

| | Before | After |
|---|---|---|
| Max gross exposure | 10.83 (cap 2.0) | 1.33 |
| Worst daily return | +513% | +1.9% |
| Costs on a 1,000,000 book | 1,026,250 | 23,870 |
| Traded notional | 10.26bn | 239m |
| Final equity | 31,148 | 1,377,304 |

Fixed by adding `Strategy::on_bar_close`, called once per timestamp after
every market event at that timestamp has been processed, so the whole
universe is marked at the same close before any weight is computed. The
per-symbol `on_market` hook remains for single-symbol strategies.

Worth recording because the failure mode was not a crash: it was a plausible
looking backtest that had quietly destroyed 97% of the capital.
