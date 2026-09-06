#pragma once

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "backtest/types.hpp"

namespace backtest {

// One symbol ready to be sized: its signal direction and its annualised
// volatility estimate.
struct SizingInput {
    Symbol symbol;
    double direction = 0.0;            // +1, -1 or 0
    double annualised_volatility = 0.0;
};

struct SizedPosition {
    Symbol symbol;
    double weight = 0.0;
};

struct SizingConfig {
    // Annualised volatility target per position, before diversification.
    double volatility_target = 0.10;
    // Cap on the sum of absolute weights.
    double max_gross_leverage = 2.0;
    // A volatility estimate at or below this is treated as unusable rather
    // than inverted. Dividing by a near-zero vol is how a sizing rule turns
    // a quiet asset into an enormous position.
    double min_volatility = 1e-6;
};

// Inverse-volatility sizing.
//
//     weight_i = direction_i * (volatility_target / sigma_i) * (1 / N)
//
// with N the number of symbols carrying an active signal, so that each
// position contributes roughly equal risk and the book does not become a bet
// on whichever asset happens to be most volatile. Gross leverage is then
// capped, scaling every weight down proportionally so the relative sizing
// the rule chose is preserved.
//
// Sizing is where a trend strategy actually earns its risk-adjusted return;
// it matters more than the signal does, which is why it is a separate,
// separately tested component rather than a few lines inside the strategy.
class PositionSizer {
public:
    explicit PositionSizer(SizingConfig config = {}) : config_(config) {
        if (!(config_.volatility_target > 0.0)) {
            throw std::invalid_argument("PositionSizer: volatility target must be positive");
        }
        if (!(config_.max_gross_leverage > 0.0)) {
            throw std::invalid_argument("PositionSizer: leverage cap must be positive");
        }
    }

    std::vector<SizedPosition> size(const std::vector<SizingInput>& inputs) const {
        // N counts only symbols that will actually take a position, so a
        // flat signal does not quietly shrink everything else.
        std::size_t active = 0;
        for (const SizingInput& input : inputs) {
            if (is_tradeable(input)) {
                ++active;
            }
        }

        std::vector<SizedPosition> positions;
        positions.reserve(inputs.size());
        if (active == 0) {
            for (const SizingInput& input : inputs) {
                positions.push_back(SizedPosition{input.symbol, 0.0});
            }
            return positions;
        }

        const double share = 1.0 / static_cast<double>(active);
        double gross = 0.0;
        for (const SizingInput& input : inputs) {
            double weight = 0.0;
            if (is_tradeable(input)) {
                weight = input.direction * (config_.volatility_target / input.annualised_volatility) *
                         share;
            }
            gross += std::abs(weight);
            positions.push_back(SizedPosition{input.symbol, weight});
        }

        // Scale proportionally rather than truncating the largest positions,
        // so the relative risk allocation survives the cap.
        if (gross > config_.max_gross_leverage && gross > 0.0) {
            const double scale = config_.max_gross_leverage / gross;
            for (SizedPosition& position : positions) {
                position.weight *= scale;
            }
        }

        return positions;
    }

    const SizingConfig& config() const noexcept { return config_; }

private:
    bool is_tradeable(const SizingInput& input) const {
        return input.direction != 0.0 && input.annualised_volatility > config_.min_volatility &&
               std::isfinite(input.annualised_volatility);
    }

    SizingConfig config_;
};

}  // namespace backtest
