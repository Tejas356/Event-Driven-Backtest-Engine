#pragma once

#include <map>
#include <optional>
#include <vector>

#include "backtest/statistics.hpp"
#include "backtest/strategy.hpp"
#include "position_sizer.hpp"
#include "tsmom_signal.hpp"

namespace backtest {

struct TsmomConfig {
    std::size_t lookback = 252;
    double volatility_halflife = 60.0;
    std::size_t volatility_seed = 20;
    SizingConfig sizing{};
};

// Time-series momentum across a universe: sign of the trailing return,
// inverse-volatility sizing, gross leverage capped.
//
// All the work happens on the first market event of a rebalance day. By then
// the data handler has already advanced every symbol to the current
// timestamp, so each symbol is reachable through latest_bars() at today's
// close -- and no further, which is what keeps this honest.
class TsmomStrategy : public Strategy {
public:
    explicit TsmomStrategy(std::vector<Symbol> universe, TsmomConfig config = {})
        : universe_(std::move(universe)), config_(config), signal_(config.lookback),
          sizer_(config.sizing) {
        for (const Symbol& symbol : universe_) {
            volatility_.emplace(symbol,
                                EwmaVolatility{config.volatility_halflife, config.volatility_seed});
        }
    }

    // Nothing happens per symbol. Sizing the book needs every symbol marked
    // at the same close, which is only true once the bar is finished.
    std::vector<SignalEvent> on_market(const MarketEvent&, const DataHandler&) override {
        return {};
    }

    std::vector<SignalEvent> on_bar_close(Timestamp ts, const DataHandler& data) override {
        update_volatility(data);

        // Rebalanced every day. Step 11 introduces the month-end schedule,
        // which is what the strategy actually runs on; daily is kept as the
        // comparison that makes the turnover argument concrete.
        ++rebalances_;
        return build_signals(ts, data);
    }

    const char* name() const noexcept override { return "tsmom"; }

    std::size_t rebalance_count() const noexcept { return rebalances_; }
    const TsmomConfig& config() const noexcept { return config_; }

private:
    // Updates every symbol from its own latest bar, not just the one whose
    // event triggered this call, so all volatilities are current as of the
    // same close and the sizing compares like with like.
    void update_volatility(const DataHandler& data) {
        for (const Symbol& symbol : universe_) {
            if (!data.traded(symbol)) {
                continue;  // no real bar today: a zero return here is fiction
            }
            const std::span<const Bar> bars = data.latest_bars(symbol, 2);
            if (bars.size() < 2 || !(bars[0].close > 0.0)) {
                continue;
            }
            volatility_.at(symbol).update(bars[1].close / bars[0].close - 1.0);
        }
    }

    std::vector<SignalEvent> build_signals(Timestamp ts, const DataHandler& data) const {
        std::vector<SizingInput> inputs;
        inputs.reserve(universe_.size());

        for (const Symbol& symbol : universe_) {
            const EwmaVolatility& vol = volatility_.at(symbol);
            const std::span<const Bar> bars = data.latest_bars(symbol, signal_.warmup_bars());
            const std::optional<double> direction = signal_.direction(bars);

            // A symbol without enough history, or whose volatility estimator
            // has not warmed up, takes no position -- rather than taking a
            // position sized off an estimate that does not exist yet.
            if (!direction.has_value() || !vol.is_warmed_up()) {
                inputs.push_back(SizingInput{symbol, 0.0, 0.0});
                continue;
            }
            inputs.push_back(SizingInput{symbol, *direction, vol.annualised()});
        }

        std::vector<SignalEvent> signals;
        signals.reserve(universe_.size());
        for (const SizedPosition& position : sizer_.size(inputs)) {
            signals.push_back(SignalEvent{ts, position.symbol, position.weight});
        }
        return signals;
    }

    std::vector<Symbol> universe_;
    TsmomConfig config_;
    TsmomSignal signal_;
    PositionSizer sizer_;
    std::map<Symbol, EwmaVolatility> volatility_;

    std::size_t rebalances_ = 0;
};

}  // namespace backtest
