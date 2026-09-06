#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "backtest/execution.hpp"
#include "backtest/metrics.hpp"
#include "backtest/portfolio.hpp"
#include "backtest/types.hpp"

namespace backtest {

// Everything a run needs. Held in one struct so that the parameters of a
// result are a single object that can be written next to it -- a metrics file
// that does not record what produced it is not reproducible.
struct BacktestConfig {
    std::filesystem::path data_dir;
    std::filesystem::path output_dir = "results";
    std::vector<Symbol> symbols{"DBC", "EEM", "EFA", "GLD", "IEF", "SPY", "TLT", "UUP"};

    std::size_t lookback = 252;
    double volatility_halflife = 60.0;
    std::size_t volatility_seed = 20;
    double volatility_target = 0.10;
    double max_gross_leverage = 2.0;
    bool monthly_rebalance = true;

    double initial_capital = 1000000.0;
    double min_trade_fraction = 0.0005;
    CostModel costs{};
    MetricsConfig metrics{};
};

// Parses `key = value` lines, ignoring blank lines and those beginning with
// '#'. Deliberately not a general TOML parser: a config format with one
// obvious meaning per line is easier to trust than one with sections,
// quoting rules and type coercion.
BacktestConfig load_config(const std::filesystem::path& file, BacktestConfig defaults = {});

// Applies `--key=value` arguments over a config, so a sweep can vary one
// parameter without writing a file per combination.
BacktestConfig apply_overrides(BacktestConfig config, const std::vector<std::string>& arguments);

std::string to_iso_date(Timestamp ts);

}  // namespace backtest
