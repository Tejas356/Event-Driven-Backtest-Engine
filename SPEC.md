# Event-Driven Backtest Engine in C++

## What this project is

A single-threaded, event-driven backtesting engine written in modern C++, with a
time-series momentum (TSMOM) strategy running on top of it.

**The engine is the deliverable. The strategy is the thing that proves the engine works.**

Read that twice, because it should shape every decision. TSMOM is arithmetically
trivial — a mean, a sign, and a division. Nobody is impressed by momentum implemented
in C++. What is worth building is an engine where look-ahead bias is *structurally
impossible* rather than merely avoided by careful coding, where rolling statistics
update in O(1) rather than recomputing over a window, and where costs are charged
honestly on every unit of turnover.

### Why event-driven rather than vectorised

In a vectorised backtest (the normal pandas approach), look-ahead bias is something
you avoid by being disciplined. One misaligned `.shift()` and your results are silently
wrong in a way that inflates them.

In an event-driven engine, the strategy object is only ever *handed* data timestamped
at or before the current event. It cannot see the future because it is never given the
future. Correctness comes from the architecture, not from remembering to be careful.

This is also the honest reason to use C++ here: an event loop is inherently sequential.
Bar N+1's processing depends on the state after bar N, so there is nothing to vectorise
and NumPy cannot rescue you. This is a genuine case where the compiled language earns
its place, unlike research code where the bottleneck is human iteration speed.

---

## Non-negotiable design principles

These are the ones that matter. If a design decision conflicts with one of these, the
principle wins.

1. **The strategy cannot access future data.** The `DataHandler` API must make this
   impossible, not merely discouraged. There is no method anywhere that returns the
   full price series. A strategy asks for the last *n* bars and receives only bars whose
   timestamp is ≤ the current simulation time.

2. **Signals are lagged.** A signal computed from bar *t*'s close executes at bar *t+1*.
   No exceptions. Most fake backtest profits come from omitting this.

3. **Costs are charged on turnover, always.** Every change in position pays. Report gross
   and net side by side so the cost drag is visible rather than buried.

4. **Rolling statistics are incremental.** Welford for variance, EWMA for volatility.
   O(1) per update. Recomputing a 60-day standard deviation from scratch on every bar
   is the thing this engine exists to not do.

5. **Every numerical component is unit tested against a known answer.** Not "it runs" —
   tested against hand-computed values or a reference implementation.

6. **No results are trusted until the harness is validated.** Step 6 exists for this and
   must not be skipped.

---

## Tech stack

- **C++20**, single-threaded. Concurrency adds nothing here and obscures the design.
- **CMake** (≥3.20) with `FetchContent` for dependencies.
- **GoogleTest** for unit tests.
- **No external numerical libraries.** The statistics are simple enough to write, and
  writing them is the point.
- **Python 3** only for two peripheral scripts: fetching data, and plotting results.
  These are not part of the engine.

Compiler flags: `-Wall -Wextra -Wpedantic`. Warnings are errors in CI if you set one up.

---

## Architecture

```
                 ┌──────────────┐
                 │ DataHandler  │  streams bars, exposes bounded lookback only
                 └──────┬───────┘
                        │ MarketEvent
                        ▼
    ┌───────────────────────────────────────┐
    │            EventQueue                 │  time-ordered FIFO
    └───────────────────────────────────────┘
         │            │            │
    MarketEvent  SignalEvent  OrderEvent/FillEvent
         ▼            ▼            ▼
    ┌─────────┐  ┌──────────┐  ┌─────────────────┐
    │Strategy │  │Portfolio │  │ExecutionHandler │
    └─────────┘  └──────────┘  └─────────────────┘
      emits        emits           emits
      Signal       Order           Fill
```

**The loop:**

```
while (data_handler.has_more_bars()) {
    data_handler.advance();                  // pushes MarketEvent(s) for this timestamp
    while (!queue.empty()) {
        event = queue.pop();
        switch (event.type) {
            MARKET: strategy.on_market(event);      // may push SignalEvent
                    portfolio.mark_to_market(event);
            SIGNAL: portfolio.on_signal(event);     // may push OrderEvent
            ORDER:  execution.on_order(event);      // pushes FillEvent
            FILL:   portfolio.on_fill(event);       // updates positions and cash
        }
    }
    portfolio.record_equity(current_time);
}
```

The outer loop advances time. The inner loop drains everything that happens *at* that
time. This is what keeps causality straight.

### Components

**`DataHandler`** — owns the price data. Streams bars in timestamp order. Its only
read API is `latest_bars(symbol, n)`, returning at most *n* bars ending at the current
simulation time. Internally it keeps a cursor; bars beyond the cursor are not reachable
through any public method.

**`Strategy`** — abstract base class. Receives `MarketEvent`, may emit `SignalEvent`
containing a *target weight* (not an order size — sizing is the portfolio's job).

**`Portfolio`** — holds positions, cash, and the equity curve. Converts target weights
into `OrderEvent`s by comparing target to current holdings. Marks to market on every
`MarketEvent`.

**`ExecutionHandler`** — models the fill. Applies slippage and commission, emits
`FillEvent`. Keeping this separate means you can later swap in a more realistic model
(participation limits, partial fills) without touching anything else.

---

## Data

Eight to ten liquid ETFs spanning asset classes. Diversity across asset classes is what
makes trend following work — a basket of correlated equity ETFs is not diversification.

| Ticker | Exposure |
|---|---|
| SPY | US equity |
| EFA | Developed international equity |
| EEM | Emerging market equity |
| IEF | US 7–10y Treasuries |
| TLT | US 20y+ Treasuries |
| GLD | Gold |
| DBC | Broad commodities |
| UUP | US dollar index |

Daily adjusted closes, from 2007 to present (DBC and UUP both launched in 2006–07, which
sets the start date). Adjusted closes handle splits and dividends; do not use raw closes.

`data/fetch_data.py` downloads these via `yfinance` and writes one CSV per symbol with
columns `date,open,high,low,close,volume`, sorted ascending, no missing rows.

---

## Build plan

Fourteen steps. Each is one commit. Do not start a step until the previous step's
acceptance criteria pass. Commit messages are given — use them.

---

### Step 1 — Scaffolding

`feat: project scaffolding with cmake and googletest`

Create:

```
.
├── CMakeLists.txt
├── .gitignore
├── README.md
├── include/backtest/
├── src/
├── strategies/
├── apps/
├── tests/
├── data/
└── analysis/
```

CMake builds a `backtest` static library, links `apps/` executables against it, and
pulls GoogleTest via `FetchContent`. `.gitignore` covers `build/`, `data/*.csv`,
`results/`.

**Acceptance:** `cmake -B build && cmake --build build && ctest --test-dir build` runs
with zero tests and exits clean.

---

### Step 2 — Core value types

`feat: core value types for timestamps, bars and identifiers`

`include/backtest/types.hpp`:

- `using Timestamp = std::chrono::sys_days;` — daily bars, no intraday complexity.
- `using Symbol = std::string;` (a `SymbolId` interning scheme is a reasonable later
  optimisation; do not do it yet)
- `struct Bar { Timestamp ts; double open, high, low, close; long volume; };`
- `using Price = double; using Quantity = double;` — fractional shares are fine, this
  is a research engine.

Keep this header free of logic. Types only.

**Acceptance:** compiles; a trivial test constructs a `Bar` and checks field access.

---

### Step 3 — Events and the queue

`feat: event types and time-ordered event queue`

`event.hpp`:

```cpp
struct MarketEvent { Timestamp ts; Symbol symbol; Bar bar; };
struct SignalEvent { Timestamp ts; Symbol symbol; double target_weight; };
struct OrderEvent  { Timestamp ts; Symbol symbol; double quantity; };  // signed
struct FillEvent   { Timestamp ts; Symbol symbol; double quantity;
                     Price fill_price; double commission; double slippage; };

using Event = std::variant<MarketEvent, SignalEvent, OrderEvent, FillEvent>;
```

`event_queue.hpp` — a FIFO wrapper over `std::deque<Event>` with `push`, `pop`,
`empty`, `size`. Within a single timestamp, FIFO ordering is the correct causal order,
so a priority queue is unnecessary. If you later add intraday data with multiple
timestamps in flight, revisit this.

Dispatch with `std::visit` and an overload set, not a type tag and a switch.

**Acceptance:** tests confirm FIFO ordering and that `std::visit` dispatches each of the
four types to the right handler.

---

### Step 4 — Incremental statistics

`feat: online mean, variance and ewma volatility estimators`

This is the first component with real content. `statistics.hpp`:

**`WelfordAccumulator`** — online mean and variance:

```
count += 1
delta  = x - mean
mean  += delta / count
M2    += delta * (x - mean)      // note: uses the UPDATED mean
variance = M2 / (count - 1)      // sample variance
```

**`EwmaVolatility`** — exponentially weighted volatility of returns:

```
lambda = exp(-ln(2) / halflife)
var_t  = lambda * var_{t-1} + (1 - lambda) * r_t^2
sigma  = sqrt(var_t)
```

Note this is the zero-mean form, which is standard for short-horizon return volatility
— daily mean returns are negligible relative to their standard deviation and estimating
them adds noise.

Seed `var_0` from the first *k* observations rather than from zero, and expose an
`is_warmed_up()` flag. A strategy must not act on an unwarmed estimator.

**`RollingWindow<T>`** — fixed-capacity ring buffer over `std::vector<T>`, O(1) push,
indexable from the back. Needed for the momentum lookback.

**Acceptance — this is the most important test file in the project:**
- Welford's mean and variance match `std::accumulate` / a hand-computed value to 1e-12
  on a known sequence.
- Welford is numerically stable: feed `{1e9 + 4, 1e9 + 7, 1e9 + 13, 1e9 + 16}`; the
  naive sum-of-squares formula loses catastrophic precision here and Welford does not.
  Assert variance ≈ 30.
- EWMA with `halflife = 1` reproduces hand-computed values for a three-element sequence.
- EWMA of a constant-magnitude return series converges to that magnitude.
- Ring buffer wraps correctly and reports the right element after overflow.

---

### Step 5 — DataHandler

`feat: csv data handler with bounded lookback window`

Loads all symbol CSVs, builds a union of all timestamps, and streams forward.

The critical API:

```cpp
class DataHandler {
public:
    bool has_more_bars() const;
    std::vector<MarketEvent> advance();              // emits bars at the next timestamp
    std::span<const Bar> latest_bars(const Symbol&, size_t n) const;
    Timestamp current_time() const;
private:
    size_t cursor_;   // nothing at index > cursor_ is reachable publicly
};
```

`latest_bars` returns bars in `[cursor_ - n + 1, cursor_]`, clamped at the start of
history. Returns fewer than *n* if insufficient history exists; the caller checks size.

**There is no method that returns the full series.** Not for convenience, not for
testing. If a test needs full data it constructs it independently.

Handle missing bars by forward-filling within a symbol and skipping symbols with no
data yet at a given timestamp — do not silently misalign series.

**Acceptance:**
- Loads a small synthetic CSV set and streams the expected number of timestamps.
- `latest_bars(sym, 5)` at cursor 2 returns 3 bars, all with `ts <= current_time()`.
- **A dedicated look-ahead test:** iterate the whole dataset and assert on every call
  that `max(bar.ts for bar in latest_bars(...)) <= current_time()`. This test is the
  project's central correctness claim — write it deliberately, not as an afterthought.

---

### Step 6 — Portfolio, execution, and the loop

`feat: portfolio accounting, execution model and event loop`

**`ExecutionHandler`** fills at the *next bar's open* — this is where signal lag is
physically enforced. Costs:

- Commission: 0.5 basis points of notional per side.
- Slippage: 0.5 basis points of notional per side, adverse (buys fill up, sells fill down).

Together, 2bps round trip. That is deliberately conservative for liquid ETFs where real
costs are lower; if the strategy survives a pessimistic assumption it survives reality.
Make both configurable.

**`Portfolio`** tracks per-symbol quantity, cash, and total equity. On `SignalEvent`
(a target weight) it computes the required order as
`target_notional / price - current_quantity` and emits an `OrderEvent` only if the
change exceeds a minimum threshold — this prevents churning on trivial weight drift and
is a real technique, not a shortcut. Records `(timestamp, equity, gross_exposure)` on
every bar.

**`Engine`** wires it together and runs the loop from the architecture section.

**Acceptance:**
- Buy 100 units at 50, sell at 55, assert final cash equals starting cash + 500 minus
  the exact expected costs. Hand-compute the expected number.
- Equity curve length equals the number of timestamps.
- Zero-signal strategy produces a flat equity curve exactly equal to starting capital.

---

### Step 7 — Buy-and-hold, and validating the harness

`test: validate engine against known buy-and-hold spy statistics`

**Do not skip this step.** Everything downstream is meaningless if the harness is wrong,
and this is the only chance to check it against an answer you already know.

Implement `BuyAndHoldStrategy` — target weight 1.0 in SPY on the first bar, never
rebalance. Run it on real SPY data from 2007.

Compare the engine's annualised return, volatility, Sharpe and max drawdown against the
same figures computed independently in `analysis/validate.py` with pandas.

**Acceptance:**
- Engine and pandas agree to within 1e-6 on annualised return and volatility.
- Sharpe lands in the 0.4–0.6 region for a 2007-onwards window.
- Max drawdown is approximately 55% (the 2008–09 decline). If you don't see the
  financial crisis in the equity curve, something is wrong.

If these don't match, stop and fix the engine. Do not proceed with a broken harness.

---

### Step 8 — Metrics

`feat: performance metrics module`

`metrics.hpp` computing from an equity curve:

- Annualised return (CAGR), annualised volatility
- Sharpe: `mean(daily excess) / stdev(daily) * sqrt(252)`, risk-free from a config
  constant (use 2% or a constant series; do not silently assume zero)
- Max drawdown **and drawdown duration in days** — duration is the one that ends real
  strategies and almost nobody reports it
- Skewness and excess kurtosis of daily returns
- Annualised turnover: `sum(|Δ notional|) / average equity / years`
- **Gross and net variants of return and Sharpe, reported side by side**

That last item is the point of the whole module. The gap between gross and net is the
single most informative number in the output.

**Acceptance:** every metric verified against a hand-computed value on a short synthetic
equity curve. Include a curve with a known drawdown of exactly 20% lasting exactly 30
days.

---

### Step 9 — TSMOM signal

`feat: time-series momentum signal`

For each symbol at each rebalance date:

```
lookback_return = close[t] / close[t - 252] - 1
signal = sign(lookback_return)        // +1, -1, or 0
```

Long if the trailing 12-month return is positive, short if negative. Skip symbols with
insufficient history. Use `RollingWindow` from Step 4.

Note: the "skip the most recent month" convention belongs to *cross-sectional* momentum,
where short-term reversal contaminates the signal. It does not apply here. Do not add it.

**Acceptance:** on a synthetic monotonically rising series, signal is +1 after warmup;
on a falling series, -1; before warmup, no signal is emitted at all.

---

### Step 10 — Volatility targeting and sizing

`feat: inverse-volatility position sizing with leverage cap`

Sizing is where the strategy actually earns its risk-adjusted return, and it matters
more than the signal does.

```
sigma_i     = ewma_vol(daily returns, halflife=60) * sqrt(252)
weight_i    = sign_i * (sigma_target / sigma_i) * (1 / N)
```

with `sigma_target = 0.10` (10% annualised per position) and *N* the number of symbols
with an active signal.

Then cap gross leverage: if `sum(|weight_i|) > 2.0`, scale all weights down proportionally.

Emit a `SignalEvent` per symbol with its target weight.

**Acceptance:**
- Two symbols with volatilities 10% and 20% receive weights in a 2:1 ratio.
- A configuration that would breach the leverage cap is scaled so gross exposure equals
  exactly 2.0.
- Realised portfolio volatility over the full backtest lands within roughly ±3
  percentage points of target. It will not be exact — volatility forecasts are imperfect
  and correlations shift — but a large miss means a bug.

---

### Step 11 — Monthly rebalancing

`feat: monthly rebalance schedule`

Recompute signals and weights on the last trading day of each month; hold between
rebalances. Positions drift with prices in the interim, which is correct — do not
rebalance back to target daily.

Daily rebalancing multiplies turnover several-fold for negligible signal improvement.
Making this configurable lets you demonstrate that, which is worth doing.

**Acceptance:** the number of rebalance events equals the number of months in the sample;
no orders are generated between them.

---

### Step 12 — Full backtest and results output

`feat: backtest runner with csv results output`

`apps/run_backtest.cpp` — reads config (symbols, dates, parameters, costs) from a simple
file or CLI args, runs the backtest, prints the metrics table, writes to `results/`:

- `equity_curve.csv` — date, equity, gross exposure, net exposure
- `trades.csv` — every fill with costs broken out
- `positions.csv` — date, symbol, weight
- `metrics.json` — the full metrics table

**Calibration — what a correct result looks like:**

| Metric | Expected range |
|---|---|
| Net Sharpe | 0.4 – 0.9 |
| Annualised volatility | 8 – 15% |
| Max drawdown | 15 – 30% |
| Skew | Positive or near zero |
| Annualised turnover | 200 – 500% |

If you get a Sharpe above 1.5, you have a bug. Look for look-ahead first, then for costs
not being charged, then for a survivorship-free universe assumption.

Be aware that trend following had a genuinely poor decade from roughly 2010 to 2019 and
a very strong 2022. A backtest starting in 2007 includes both. A mediocre Sharpe here is
an honest result, not a failure — and reporting it honestly is more impressive than
tuning until it looks good.

---

### Step 13 — Parameter sweep

`feat: parameter sweep across lookback windows`

`apps/sweep.cpp` — run the backtest across lookbacks of 63, 126, 189, 252, 378 trading
days and across volatility targets, writing a grid of results.

**This is the robustness test, and it is what separates a real result from a fitted one.**
You want a broad plateau: if 126, 189 and 252 all produce similar Sharpes, the effect is
probably real. If only 252 works and its neighbours are flat or negative, you have found
the past rather than a signal.

Report the plateau explicitly in the README. If there isn't one, say so.

---

### Step 14 — Analysis and writeup

`docs: results analysis and readme`

`analysis/analyse.py` reads `results/` and plots: equity curve (log scale), drawdown
underwater plot, rolling 12-month Sharpe, gross-vs-net cumulative return, and the sweep
heatmap.

README covers: what the engine does, the architectural argument for why look-ahead is
structurally prevented, how to build and run, the results with the plots, the sweep
plateau, and an honest limitations section.

**Limitations to state explicitly** — this section is what a reader will judge you on:
- ETF proxies rather than actual futures; real trend followers trade futures, which have
  different cost and roll characteristics.
- No borrow costs or shorting constraints modelled.
- Costs are a flat basis-point assumption, not a market-impact model.
- Single-path backtest; no bootstrap or Monte Carlo confidence intervals on the Sharpe.
- The sample includes only one major trend regime change.

Being straightforward about what the backtest doesn't capture demonstrates exactly the
judgement that most candidate projects lack.

---

## Working practices

- One commit per step, using the messages above.
- Tests pass before each commit. `ctest` clean, no exceptions.
- No step begins before the previous step's acceptance criteria are met.
- Keep a `NOTES.md` logging every parameter variation tried and its result. When a
  configuration eventually looks good, the number of variations you tried determines how
  much to discount it. Untracked, you will fool yourself — this is the single most
  common way self-directed quant projects go wrong.

## Extension directions, once this works

Only after Step 14 is complete and committed:

- A second uncorrelated strategy sleeve (carry, or cross-sectional momentum) sharing the
  same engine — this proves the strategy interface is genuinely reusable.
- Replace the flat cost model with a square-root market impact model.
- Bootstrap confidence intervals on the Sharpe ratio.
- Benchmark the engine's throughput in bars per second and profile the hot path.
