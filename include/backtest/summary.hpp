#pragma once

#include <cstddef>
#include <vector>

#include "backtest/portfolio.hpp"

namespace backtest {

// The four statistics needed to validate the harness against a known answer.
// Step 8 replaces this with a full metrics module; it is kept minimal here so
// that the validation depends on as little unvalidated code as possible.
struct SummaryStatistics {
    double annualised_return = 0.0;
    double annualised_volatility = 0.0;
    double sharpe = 0.0;
    double max_drawdown = 0.0;
    std::size_t observations = 0;
};

// Daily equity series in, summary out. Returns are simple daily returns of
// the equity curve; annualisation uses 252 trading days.
SummaryStatistics summarise(const std::vector<EquityPoint>& curve,
                            double risk_free_rate = 0.02,
                            double periods_per_year = 252.0);

}  // namespace backtest
