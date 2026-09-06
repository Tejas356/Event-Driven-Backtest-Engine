#include "backtest/execution.hpp"

#include <cmath>

namespace backtest {

void ExecutionHandler::submit(const OrderEvent& order) {
    if (order.quantity == 0.0) {
        return;
    }
    // A newer order on the same symbol supersedes the resting one rather than
    // stacking with it: both express a target, and the later one is current.
    pending_[order.symbol] = order;
}

std::vector<FillEvent> ExecutionHandler::fill_pending(const MarketEvent& event) {
    const auto it = pending_.find(event.symbol);
    if (it == pending_.end()) {
        return {};
    }

    const OrderEvent order = it->second;
    pending_.erase(it);

    // Fill at this bar open. The order was generated from a previous bar
    // close, so the lag is one bar by construction.
    const Price reference = event.bar.open;
    const double direction = order.quantity > 0.0 ? 1.0 : -1.0;

    // Slippage is adverse in both directions: buys fill up, sells fill down.
    const Price fill_price = reference * (1.0 + direction * costs_.slippage_rate());
    const double notional = std::abs(order.quantity) * fill_price;

    const double commission = costs_.commission_rate() * notional;
    // Recorded as a cost in currency so that gross and net can be reported
    // side by side rather than the drag being buried in the fill price.
    const double slippage = std::abs(order.quantity) * std::abs(fill_price - reference);

    return {FillEvent{.ts = event.ts,
                      .symbol = order.symbol,
                      .quantity = order.quantity,
                      .fill_price = fill_price,
                      .commission = commission,
                      .slippage = slippage}};
}

}  // namespace backtest
