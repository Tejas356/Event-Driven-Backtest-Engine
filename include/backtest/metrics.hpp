#pragma once

#include <cstddef>
#include <vector>

#include "backtest/portfolio.hpp"
#include "backtest/types.hpp"

namespace backtest {

// Performance of one equity series.
struct PerformanceMetrics {
    double annualised_return = 0.0;
    double annualised_volatility = 0.0;
    double sharpe = 0.0;
    double max_drawdown = 0.0;
    // Longest stretch, in calendar days, between an equity peak and the day
    // the curve regains it. Depth is what gets reported; duration is what
    // ends real strategies, and almost nobody reports it.
    long max_drawdown_days = 0;
    double skewness = 0.0;
    double excess_kurtosis = 0.0;
    std::size_t observations = 0;
};

// Gross and net side by side. The gap between them is the single most
// informative number the engine produces, which is why it is a first-class
// result rather than something a reader has to work out.
struct BacktestMetrics {
    PerformanceMetrics gross;
    PerformanceMetrics net;
    // Sum of absolute traded notional over average equity, per year.
    double annualised_turnover = 0.0;
    double total_commission = 0.0;
    double total_slippage = 0.0;
    // net annualised return subtracted from gross: the cost drag in return
    // terms, which is the figure worth quoting.
    double cost_drag = 0.0;
    double years = 0.0;
    double initial_equity = 0.0;
    double final_equity = 0.0;
};

struct MetricsConfig {
    // Never silently zero: a zero risk-free rate flatters every Sharpe in a
    // sample containing 2007 and 2023, when cash paid 5%.
    double risk_free_rate = 0.02;
    double periods_per_year = 252.0;
};

// Metrics of an arbitrary equity series, sampled once per trading day.
PerformanceMetrics compute_performance(const std::vector<double>& equity,
                                       const std::vector<Timestamp>& dates,
                                       MetricsConfig config = {});

// Metrics of a completed run, gross and net.
BacktestMetrics compute_metrics(const Portfolio& portfolio, MetricsConfig config = {});

}  // namespace backtest
