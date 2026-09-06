#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "backtest/types.hpp"

namespace backtest {

// Time-series momentum signal (Moskowitz, Ooi and Pedersen 2012).
//
//     lookback_return = close[t] / close[t - lookback] - 1
//     signal          = sign(lookback_return)
//
// Long if the trailing return is positive, short if negative. That is the
// whole of it -- the arithmetic is trivial, and the strategy is here to
// exercise the engine rather than to be clever.
//
// Note on a convention deliberately not applied: cross-sectional momentum
// usually skips the most recent month, because short-horizon reversal
// contaminates the ranking. Time-series momentum has no such problem, since
// there is no ranking against other assets. Adding the skip here would be
// cargo-culting a fix for a different strategy.
class TsmomSignal {
public:
    explicit TsmomSignal(std::size_t lookback = 252) : lookback_(lookback) {}

    // Returns nullopt when history is too short to form the signal, so a
    // caller cannot accidentally treat "not enough data" as "no trend".
    std::optional<double> lookback_return(std::span<const Bar> bars) const {
        // Comparing close[t] against close[t - lookback] needs lookback + 1
        // bars, not lookback. Off by one here silently shifts every signal.
        if (bars.size() < lookback_ + 1) {
            return std::nullopt;
        }
        const Price now = bars.back().close;
        const Price then = bars[bars.size() - 1 - lookback_].close;
        if (!(then > 0.0)) {
            return std::nullopt;
        }
        return now / then - 1.0;
    }

    // +1, -1, or 0 for an exactly flat lookback return.
    std::optional<double> direction(std::span<const Bar> bars) const {
        const std::optional<double> ret = lookback_return(bars);
        if (!ret) {
            return std::nullopt;
        }
        if (*ret > 0.0) {
            return 1.0;
        }
        if (*ret < 0.0) {
            return -1.0;
        }
        return 0.0;
    }

    std::size_t lookback() const noexcept { return lookback_; }
    // Bars needed before the signal is defined.
    std::size_t warmup_bars() const noexcept { return lookback_ + 1; }

private:
    std::size_t lookback_;
};

}  // namespace backtest
