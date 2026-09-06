#pragma once

#include <variant>

#include "backtest/types.hpp"

namespace backtest {

// A bar has become available at ts. This is the only way price data enters the
// system, and the only thing a strategy is ever shown.
struct MarketEvent {
    Timestamp ts;
    Symbol symbol;
    Bar bar;
};

// A strategy's view: how much of the portfolio it wants in this symbol.
// Deliberately a weight and not a size -- sizing is the portfolio's job, and a
// strategy that could name a quantity would need to know the equity.
struct SignalEvent {
    Timestamp ts;
    Symbol symbol;
    double target_weight;
};

// Signed: positive buys, negative sells.
struct OrderEvent {
    Timestamp ts;
    Symbol symbol;
    Quantity quantity;
};

// Costs are carried on the fill rather than netted into fill_price, so gross
// and net performance can be reported side by side later.
struct FillEvent {
    Timestamp ts;
    Symbol symbol;
    Quantity quantity;
    Price fill_price;
    double commission;
    double slippage;
};

using Event = std::variant<MarketEvent, SignalEvent, OrderEvent, FillEvent>;

// Overload set for std::visit, so dispatch is a compile-time exhaustiveness
// check rather than a switch on a type tag that can silently miss a case.
template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

// The time an event occurs, whichever alternative it holds.
inline Timestamp event_time(const Event& event) {
    return std::visit([](const auto& e) { return e.ts; }, event);
}

}  // namespace backtest
