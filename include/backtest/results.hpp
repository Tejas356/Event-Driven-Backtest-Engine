#pragma once

#include <filesystem>
#include <string>

#include "backtest/config.hpp"
#include "backtest/metrics.hpp"
#include "backtest/portfolio.hpp"

namespace backtest {

// Writes equity_curve.csv, trades.csv, positions.csv and metrics.json.
//
// metrics.json carries the configuration alongside the results, because a
// metrics file that does not record what produced it cannot be reproduced
// and should not be trusted.
void write_results(const std::filesystem::path& directory, const BacktestConfig& config,
                   const Portfolio& portfolio, const BacktestMetrics& metrics);

// The metrics table as printed to the terminal, gross beside net.
std::string format_metrics_table(const BacktestConfig& config, const BacktestMetrics& metrics);

}  // namespace backtest
