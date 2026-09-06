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

---

## Step 11 -- monthly rebalancing

Same configuration as Step 10, schedule changed to the last trading day of
each month. 237 rebalances over 4950 bars.

| | Daily | Monthly |
|---|---|---|
| Annualised return (net) | 1.64% | 1.95% |
| Annualised volatility | 4.76% | 4.83% |
| Net Sharpe (rf 2%) | -0.054 | +0.009 |
| Max drawdown | 11.2% (1180 days) | 12.9% (1599 days) |
| Annualised turnover | 1087% | 259% |
| Costs on 1,000,000 | 23,870 | 5,863 |
| Skew | -0.47 | -0.19 |

Turnover falls 4.2x and lands inside the 200-500% the plan expects. Daily
rebalancing was churning the book on volatility-estimate wiggles carrying no
signal, and paying for it. Monthly is better before costs as well as after,
so this is not merely a cost saving.

Calendar-year returns, confirming the strategy behaves like trend following
rather than like a bug:

    2008 +8.56%   2016 -5.47%
    2009 -3.50%   2017 +5.90%
    2010 -1.39%   2018 -5.98%
    2011 +2.93%   2019 +2.99%
    2012 +0.53%   2020 +1.17%
    2013 +7.72%   2021 -1.51%
    2014 +0.26%   2022 +7.20%
    2015 +4.89%   2023 -1.09%

2008 and 2022 are the two years trend following is supposed to win, and it
does. The flat-to-negative 2009-2012 whipsaw and the mediocre 2010-2019 are
also exactly what the literature reports.

### Open question for Step 12: the risk-free rate is being double-counted

The book runs at a mean gross exposure of 0.79, so most of the capital sits
in cash -- and the engine credits no interest on it. The Sharpe then
subtracts a 2% risk-free rate anyway, charging for a cash return that was
never earned.

At rf = 0%, which treats the equity curve as the excess-of-cash series it
effectively is, the net Sharpe is around 0.4. At rf = 2% it is 0.01. The
honest figure is between the two, nearer the former.

Not resolved by quietly picking the flattering number. Step 12 reports both
and says why; a financing model is out of scope and is listed as a
limitation.

---

## Step 12 -- full backtest, headline result

`./build/bin/run_backtest --config=config/default.toml`, 8 ETFs,
2007-01-03 to 2026-09-04, 4950 bars, 237 rebalances, 1693 fills.

| Metric | Gross | Net |
|---|---|---|
| Annualised return | 1.97% | 1.95% |
| Annualised volatility | 4.82% | 4.83% |
| Sharpe (rf 2%) | 0.014 | 0.009 |
| Sharpe (no risk-free) | 0.428 | 0.423 |
| Max drawdown | 12.80% | 12.87% |
| Drawdown duration | 1599 days | 1599 days |
| Skew | -0.193 | -0.193 |
| Excess kurtosis | 4.63 | 4.64 |
| Annualised turnover | | 259% |
| Commission / slippage | | 2932 / 2932 |

Final equity 1,460,329 from 1,000,000 over 19.64 years.

### Against the calibration the plan gives

| | Expected | Actual | |
|---|---|---|---|
| Net Sharpe | 0.4-0.9 | 0.42 (no rf) / 0.01 (rf 2%) | see below |
| Annualised volatility | 8-15% | 4.83% | below |
| Max drawdown | 15-30% | 12.87% | below |
| Skew | positive or near zero | -0.19 | near zero |
| Annualised turnover | 200-500% | 259% | in range |

Volatility and drawdown are below the expected range, and the reason is in
the plan itself rather than in the code. The sizing formula given is

    weight_i = sign_i * (sigma_target / sigma_i) * (1 / N)

with sigma_target described as 10% *per position*. With N = 8 that targets
roughly 10%/sqrt(8) = 3.5% at portfolio level, not 8-15%. Realised 4.83% is
consistent with that, once positive correlation between trend positions is
allowed for. Reaching the stated band would mean dropping the 1/N or raising
the target to about 0.28.

**Not done.** Sharpe is scale-invariant, so levering the book up would move
volatility and drawdown into the expected ranges without improving the result
by a single basis point -- it would only make the table look more like the
table. The formula as specified is what is implemented, and the discrepancy
is recorded here instead.

### The two Sharpe figures

Both are reported because neither is right on its own. The engine credits no
interest on idle cash, and at a mean gross exposure of 0.79 most of the book
is cash. Subtracting a 2% risk-free rate therefore charges the strategy for a
return it never earned, giving 0.009. Charging nothing gives 0.423. The
honest figure is between, and nearer 0.423.

Modelling cash interest properly would have meant a financing model, and
would have broken the Step 7 validation that currently agrees with pandas to
2.6e-16 unless the reference implementation grew one too. Listed as a
limitation instead.

### Parameter variations tried so far

Two, both forced by the plan rather than chosen to improve the result:
daily vs monthly rebalancing (Step 11), and costs on vs off (Steps 10-12).
No parameter has yet been varied in search of a better number. Step 13 is the
first place that happens, and it is a sweep reported in full rather than a
search reported at its best point.

---

## Step 13 -- parameter sweep

`./build/bin/sweep --config=config/default.toml`, 20 runs, 5 lookbacks x 4
volatility targets, monthly rebalancing, 2bp round trip. Full grid in
`results/sweep.csv`; the whole grid is reported, not its best cell.

Net Sharpe, risk-free deduction removed (see the Step 12 note on idle cash):

| Lookback | 0.05 | 0.10 | 0.15 | 0.20 | Turnover @0.10 |
|---|---|---|---|---|---|
| 63 | 0.507 | 0.509 | 0.511 | 0.502 | 546% |
| 126 | 0.619 | 0.621 | 0.623 | 0.609 | 353% |
| 189 | 0.488 | 0.490 | 0.493 | 0.486 | 304% |
| 252 | 0.422 | 0.423 | 0.424 | 0.424 | 259% |
| 378 | 0.537 | 0.536 | 0.536 | 0.531 | 253% |

**There is a plateau.** All five lookbacks are positive and sit in a band of
0.42 to 0.62. Nothing spikes: the best cell, 126 days, beats its neighbours
by roughly 0.11 and 0.13, which is well inside what a Sharpe standard error
over 19.6 years admits (about 1/sqrt(19.6) = 0.23). The result does not
depend on the lookback being right.

The 252-day value the literature uses is the *worst* of the five. That is
worth stating plainly: had the plan not fixed 252 in advance, picking 126
from this grid would have been fitting, and the honest reading is that the
five values are indistinguishable.

Rows are nearly flat across volatility targets, as they must be -- Sharpe is
scale-invariant, and the target only changes the result through costs, which
is why the 0.20 column is very slightly worse. Asserted as a code property in
tests/test_robustness.cpp rather than left as an observation.

Turnover falls monotonically with lookback, from 546% at 63 days to 253% at
378. A shorter lookback flips sign more often and pays for it.

Total parameter variations tried across the whole project: 22. Two forced by
the plan (daily vs monthly, costs on vs off) and the 20 cells of this grid.
None chosen to improve the headline figure, which remains the 252-day
configuration fixed before any of this was run.
