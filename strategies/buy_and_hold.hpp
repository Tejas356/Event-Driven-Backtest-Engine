#pragma once

#include <utility>
#include <vector>

#include "backtest/strategy.hpp"

namespace backtest {

// Buys one symbol on the first bar it sees and never trades again.
//
// This exists to validate the harness, not to make money. It is the only
// strategy whose correct answer is known in advance from the price series
// alone, which makes it the one chance to check the engine against an
// independent computation before anything harder is trusted.
class BuyAndHoldStrategy : public Strategy {
public:
    explicit BuyAndHoldStrategy(Symbol symbol, double weight = 1.0)
        : symbol_(std::move(symbol)), weight_(weight) {}

    std::vector<SignalEvent> on_market(const MarketEvent& event, const DataHandler&) override {
        if (invested_ || event.symbol != symbol_) {
            return {};
        }
        invested_ = true;
        return {SignalEvent{event.ts, symbol_, weight_}};
    }

    const char* name() const noexcept override { return "buy_and_hold"; }

private:
    Symbol symbol_;
    double weight_;
    bool invested_ = false;
};

}  // namespace backtest
