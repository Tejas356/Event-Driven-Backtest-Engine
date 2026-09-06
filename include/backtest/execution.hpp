#pragma once

#include <cstddef>
#include <map>
#include <vector>

#include "backtest/event.hpp"
#include "backtest/types.hpp"

namespace backtest {

// Transaction costs, in basis points of traded notional, charged per side.
// The defaults are deliberately pessimistic for liquid ETFs: if a strategy
// survives an assumption worse than reality, it survives reality.
struct CostModel {
    double commission_bps = 0.5;
    double slippage_bps = 0.5;

    double commission_rate() const noexcept { return commission_bps * 1e-4; }
    double slippage_rate() const noexcept { return slippage_bps * 1e-4; }
};

// Turns orders into fills.
//
// This is where signal lag stops being a convention and becomes physical. An
// order is never filled when it is submitted; it is held until the *next*
// market event for that symbol arrives, and filled at that bar open. The
// handler has no access to the DataHandler at all, so there is no code path
// by which it could fill at the price that generated the signal -- not
// because that would be wrong, but because it cannot reach the data to do it.
class ExecutionHandler {
public:
    explicit ExecutionHandler(CostModel costs = {}) : costs_(costs) {}

    // Accepts an order for execution at the next available bar.
    void submit(const OrderEvent& order);

    // Called when a market event arrives. Fills any order resting on that
    // symbol at this bar open, applying adverse slippage and commission.
    std::vector<FillEvent> fill_pending(const MarketEvent& event);

    bool has_pending() const noexcept { return !pending_.empty(); }
    std::size_t pending_count() const noexcept { return pending_.size(); }
    const CostModel& costs() const noexcept { return costs_; }

private:
    CostModel costs_;
    // At most one resting order per symbol: a second order on the same
    // symbol before the first fills replaces it, since it reflects a newer
    // view of the same target.
    std::map<Symbol, OrderEvent> pending_;
};

}  // namespace backtest
