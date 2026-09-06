#pragma once

#include <vector>

#include "backtest/data_handler.hpp"
#include "backtest/event.hpp"

namespace backtest {

// Base class for strategies.
//
// A strategy is handed one market event and a const view of the data, and
// returns the signals it wants to emit. It deliberately does not receive the
// event queue: a strategy that could push arbitrary events could push a
// FillEvent and mark its own trades. Returning signals makes emitting
// anything else impossible rather than merely bad manners.
//
// It also does not receive the portfolio. A strategy names a target weight;
// what that means in shares, and whether the resulting trade is worth making,
// is the portfolio's decision. Keeping equity out of the strategy is what
// stops position sizing leaking into signal logic.
class Strategy {
public:
    virtual ~Strategy() = default;

    Strategy() = default;
    Strategy(const Strategy&) = default;
    Strategy& operator=(const Strategy&) = default;
    Strategy(Strategy&&) = default;
    Strategy& operator=(Strategy&&) = default;

    // Called once per market event. The DataHandler is const and bounded to
    // the current simulation time, so every symbol is reachable through
    // latest_bars() but no future bar is.
    //
    // Suitable for a single-symbol strategy. A portfolio strategy should use
    // on_bar_close instead: market events arrive one symbol at a time, so a
    // strategy that sizes the whole book here would be doing it while most of
    // the universe is still marked at yesterday prices.
    virtual std::vector<SignalEvent> on_market(const MarketEvent& event,
                                               const DataHandler& data) = 0;

    // Called once per timestamp, after every market event at that timestamp
    // has been processed. By this point the whole universe is marked to
    // market at today close, so weights computed here compare like with like.
    // Still bounded to the current simulation time: nothing later exists yet.
    virtual std::vector<SignalEvent> on_bar_close(Timestamp, const DataHandler&) { return {}; }

    virtual const char* name() const noexcept = 0;
};

}  // namespace backtest
