#include "backtest/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "backtest/statistics.hpp"

namespace backtest {
namespace {

// Simple daily returns of an equity series.
std::vector<double> daily_returns(const std::vector<double>& equity) {
    std::vector<double> returns;
    if (equity.size() < 2) {
        return returns;
    }
    returns.reserve(equity.size() - 1);
    for (std::size_t i = 1; i < equity.size(); ++i) {
        if (equity[i - 1] != 0.0) {
            returns.push_back(equity[i] / equity[i - 1] - 1.0);
        }
    }
    return returns;
}

// Depth and duration of the worst drawdown.
//
// Duration is measured from the peak to the day the curve regains it, in
// calendar days. A drawdown still open at the end of the sample counts to the
// last date rather than being discarded: an unrecovered drawdown is the worst
// kind, and dropping it would flatter the result.
struct DrawdownResult {
    double depth = 0.0;
    long days = 0;
};

DrawdownResult worst_drawdown(const std::vector<double>& equity,
                              const std::vector<Timestamp>& dates) {
    DrawdownResult result;
    if (equity.empty()) {
        return result;
    }

    double peak = equity.front();
    std::size_t peak_index = 0;
    bool under_water = false;

    for (std::size_t i = 1; i < equity.size(); ++i) {
        if (equity[i] >= peak) {
            if (under_water) {
                const long days = (dates[i] - dates[peak_index]).count();
                result.days = std::max(result.days, days);
                under_water = false;
            }
            peak = equity[i];
            peak_index = i;
            continue;
        }

        under_water = true;
        if (peak > 0.0) {
            result.depth = std::max(result.depth, (peak - equity[i]) / peak);
        }
    }

    if (under_water) {
        const long days = (dates.back() - dates[peak_index]).count();
        result.days = std::max(result.days, days);
    }
    return result;
}

// Population moments, matching scipy.stats.skew / kurtosis with bias=True.
// Stated explicitly because the sample-corrected variants differ by enough to
// matter on a short series, and a metric whose definition is ambiguous is not
// a metric.
void higher_moments(const std::vector<double>& values, double mean, double& skewness,
                    double& excess_kurtosis) {
    skewness = 0.0;
    excess_kurtosis = 0.0;
    if (values.size() < 2) {
        return;
    }

    double m2 = 0.0;
    double m3 = 0.0;
    double m4 = 0.0;
    for (double value : values) {
        const double d = value - mean;
        const double d2 = d * d;
        m2 += d2;
        m3 += d2 * d;
        m4 += d2 * d2;
    }
    const double n = static_cast<double>(values.size());
    m2 /= n;
    m3 /= n;
    m4 /= n;

    if (m2 > 0.0) {
        skewness = m3 / std::pow(m2, 1.5);
        excess_kurtosis = m4 / (m2 * m2) - 3.0;
    }
}

}  // namespace

PerformanceMetrics compute_performance(const std::vector<double>& equity,
                                       const std::vector<Timestamp>& dates,
                                       MetricsConfig config) {
    if (equity.size() != dates.size()) {
        throw std::invalid_argument("compute_performance: equity and dates differ in length");
    }

    PerformanceMetrics metrics;
    const std::vector<double> returns = daily_returns(equity);
    metrics.observations = returns.size();
    if (returns.size() < 2) {
        return metrics;
    }

    WelfordAccumulator accumulator;
    for (double ret : returns) {
        accumulator.update(ret);
    }
    const double stddev = accumulator.stddev();

    const double years = static_cast<double>(returns.size()) / config.periods_per_year;
    const double growth = equity.back() / equity.front();
    if (growth > 0.0 && years > 0.0) {
        metrics.annualised_return = std::pow(growth, 1.0 / years) - 1.0;
    }

    metrics.annualised_volatility = stddev * std::sqrt(config.periods_per_year);
    if (stddev > 0.0) {
        const double excess = accumulator.mean() - config.risk_free_rate / config.periods_per_year;
        metrics.sharpe = excess / stddev * std::sqrt(config.periods_per_year);
    }

    const DrawdownResult drawdown = worst_drawdown(equity, dates);
    metrics.max_drawdown = drawdown.depth;
    metrics.max_drawdown_days = drawdown.days;

    higher_moments(returns, accumulator.mean(), metrics.skewness, metrics.excess_kurtosis);
    return metrics;
}

BacktestMetrics compute_metrics(const Portfolio& portfolio, MetricsConfig config) {
    const std::vector<EquityPoint>& curve = portfolio.equity_curve();

    BacktestMetrics metrics;
    if (curve.empty()) {
        return metrics;
    }

    std::vector<double> net;
    std::vector<double> gross;
    std::vector<Timestamp> dates;
    net.reserve(curve.size());
    gross.reserve(curve.size());
    dates.reserve(curve.size());

    double equity_sum = 0.0;
    for (const EquityPoint& point : curve) {
        net.push_back(point.equity);
        gross.push_back(point.gross_equity);
        dates.push_back(point.ts);
        equity_sum += point.equity;
    }

    metrics.net = compute_performance(net, dates, config);
    metrics.gross = compute_performance(gross, dates, config);

    metrics.years = static_cast<double>(metrics.net.observations) / config.periods_per_year;
    metrics.initial_equity = net.front();
    metrics.final_equity = net.back();
    metrics.total_commission = portfolio.total_commission();
    metrics.total_slippage = portfolio.total_slippage();
    metrics.cost_drag = metrics.gross.annualised_return - metrics.net.annualised_return;

    const double average_equity = equity_sum / static_cast<double>(curve.size());
    if (average_equity > 0.0 && metrics.years > 0.0) {
        metrics.annualised_turnover =
            portfolio.total_traded_notional() / average_equity / metrics.years;
    }

    return metrics;
}

}  // namespace backtest
