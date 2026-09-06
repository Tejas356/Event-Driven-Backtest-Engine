#include "backtest/summary.hpp"

#include <cmath>

#include "backtest/statistics.hpp"

namespace backtest {

SummaryStatistics summarise(const std::vector<EquityPoint>& curve, double risk_free_rate,
                            double periods_per_year) {
    SummaryStatistics stats;
    if (curve.size() < 2) {
        return stats;
    }

    // Simple daily returns of the equity curve. The conventions here are
    // restated in analysis/validate.py so the two implementations can be
    // compared without either quietly assuming a different definition.
    WelfordAccumulator daily;
    WelfordAccumulator excess;
    const double risk_free_daily = risk_free_rate / periods_per_year;

    double peak = curve.front().equity;
    double max_drawdown = 0.0;

    for (std::size_t i = 1; i < curve.size(); ++i) {
        const double previous = curve[i - 1].equity;
        const double current = curve[i].equity;
        if (previous != 0.0) {
            const double ret = current / previous - 1.0;
            daily.update(ret);
            excess.update(ret - risk_free_daily);
        }

        peak = std::max(peak, current);
        if (peak > 0.0) {
            max_drawdown = std::max(max_drawdown, (peak - current) / peak);
        }
    }

    stats.observations = daily.count();
    if (daily.count() < 2) {
        return stats;
    }

    const double years = static_cast<double>(daily.count()) / periods_per_year;
    const double total_growth = curve.back().equity / curve.front().equity;
    if (total_growth > 0.0 && years > 0.0) {
        stats.annualised_return = std::pow(total_growth, 1.0 / years) - 1.0;
    }

    const double daily_stddev = daily.stddev();
    stats.annualised_volatility = daily_stddev * std::sqrt(periods_per_year);
    if (daily_stddev > 0.0) {
        stats.sharpe = excess.mean() / daily_stddev * std::sqrt(periods_per_year);
    }
    stats.max_drawdown = max_drawdown;

    return stats;
}

}  // namespace backtest
