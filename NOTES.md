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
