// Runs one backtest and writes its results.
//
//     run_backtest [--config=FILE] [--key=value ...]
//
// Any config key can be overridden on the command line, which is what lets
// the sweep vary one parameter without writing a file per combination.

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "backtest/config.hpp"
#include "backtest/engine.hpp"
#include "backtest/metrics.hpp"
#include "backtest/results.hpp"
#include "backtest/version.hpp"
#include "tsmom.hpp"

using namespace backtest;

namespace {

TsmomStrategy make_strategy(const BacktestConfig& config) {
    return TsmomStrategy{
        config.symbols,
        TsmomConfig{.lookback = config.lookback,
                    .volatility_halflife = config.volatility_halflife,
                    .volatility_seed = config.volatility_seed,
                    .sizing = SizingConfig{.volatility_target = config.volatility_target,
                                           .max_gross_leverage = config.max_gross_leverage},
                    .frequency = config.monthly_rebalance ? RebalanceFrequency::MonthEnd
                                                          : RebalanceFrequency::Daily}};
}

}  // namespace

int main(int argc, char** argv) {
    try {
        BacktestConfig config;
        config.data_dir = TSMOM_DATA_DIR;

        std::vector<std::string> overrides;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument.rfind("--config=", 0) == 0) {
                config = load_config(argument.substr(9), config);
            } else {
                overrides.push_back(argument);
            }
        }
        config = apply_overrides(std::move(config), overrides);

        DataHandler data = DataHandler::from_csv_directory(config.data_dir, config.symbols);
        TsmomStrategy strategy = make_strategy(config);
        Portfolio portfolio{PortfolioConfig{.initial_capital = config.initial_capital,
                                            .min_trade_fraction = config.min_trade_fraction}};
        ExecutionHandler execution{config.costs};

        const RunStatistics run = Engine(data, strategy, portfolio, execution).run();
        const BacktestMetrics metrics = compute_metrics(portfolio, config.metrics);

        write_results(config.output_dir, config, portfolio, metrics);

        std::cout << "tsmom-engine " << version() << "\n\n"
                  << "  symbols                    " << config.symbols.size() << '\n'
                  << "  bars                       " << run.timestamps << '\n'
                  << "  rebalances                 " << strategy.rebalance_count() << '\n'
                  << "  orders / fills             " << run.orders << " / " << run.fills << "\n\n"
                  << format_metrics_table(config, metrics) << '\n'
                  << "  results written to " << config.output_dir.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "run_backtest: " << error.what() << '\n';
        return 1;
    }
}
