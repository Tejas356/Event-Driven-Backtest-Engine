// Runs the backtest across a grid of lookbacks and volatility targets.
//
// This is the robustness test, and it is what separates a real result from a
// fitted one. What matters is whether neighbouring parameter values agree: a
// broad plateau is evidence that the effect is there, whereas a spike at one
// lookback with flat or negative neighbours means the parameter has found
// the past rather than a signal.
//
// The whole grid is written out, not the best cell. A sweep reported at its
// maximum is a search presented as a result.

#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "backtest/config.hpp"
#include "backtest/engine.hpp"
#include "backtest/metrics.hpp"
#include "backtest/results.hpp"
#include "tsmom.hpp"

using namespace backtest;

namespace {

struct Cell {
    std::size_t lookback = 0;
    double volatility_target = 0.0;
    BacktestMetrics metrics;
    double sharpe_no_rf = 0.0;
    std::size_t rebalances = 0;
};

Cell run_one(const BacktestConfig& base, std::size_t lookback, double volatility_target) {
    BacktestConfig config = base;
    config.lookback = lookback;
    config.volatility_target = volatility_target;

    DataHandler data = DataHandler::from_csv_directory(config.data_dir, config.symbols);
    TsmomStrategy strategy{
        config.symbols,
        TsmomConfig{.lookback = config.lookback,
                    .volatility_halflife = config.volatility_halflife,
                    .volatility_seed = config.volatility_seed,
                    .sizing = SizingConfig{.volatility_target = config.volatility_target,
                                           .max_gross_leverage = config.max_gross_leverage},
                    .frequency = config.monthly_rebalance ? RebalanceFrequency::MonthEnd
                                                          : RebalanceFrequency::Daily}};
    Portfolio portfolio{PortfolioConfig{.initial_capital = config.initial_capital,
                                        .min_trade_fraction = config.min_trade_fraction}};
    ExecutionHandler execution{config.costs};
    Engine(data, strategy, portfolio, execution).run();

    Cell cell;
    cell.lookback = lookback;
    cell.volatility_target = volatility_target;
    cell.metrics = compute_metrics(portfolio, config.metrics);
    cell.sharpe_no_rf = sharpe_without_risk_free(cell.metrics.net, config.metrics.risk_free_rate);
    cell.rebalances = strategy.rebalance_count();
    return cell;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        BacktestConfig config;
        config.data_dir = TSMOM_DATA_DIR;
        config.output_dir = "results";

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

        // Neighbouring lookbacks either side of the 252-day convention, so
        // that a plateau is visible if there is one.
        const std::vector<std::size_t> lookbacks{63, 126, 189, 252, 378};
        const std::vector<double> targets{0.05, 0.10, 0.15, 0.20};

        std::vector<Cell> grid;
        grid.reserve(lookbacks.size() * targets.size());
        for (std::size_t lookback : lookbacks) {
            for (double target : targets) {
                grid.push_back(run_one(config, lookback, target));
            }
        }

        std::filesystem::create_directories(config.output_dir);
        {
            std::ofstream out(config.output_dir / "sweep.csv");
            out << std::setprecision(17)
                << "lookback,volatility_target,net_return,net_volatility,net_sharpe,"
                   "net_sharpe_no_rf,max_drawdown,max_drawdown_days,skew,turnover,"
                   "cost_drag,rebalances,final_equity\n";
            for (const Cell& cell : grid) {
                out << cell.lookback << ',' << cell.volatility_target << ','
                    << cell.metrics.net.annualised_return << ','
                    << cell.metrics.net.annualised_volatility << ',' << cell.metrics.net.sharpe
                    << ',' << cell.sharpe_no_rf << ',' << cell.metrics.net.max_drawdown << ','
                    << cell.metrics.net.max_drawdown_days << ',' << cell.metrics.net.skewness
                    << ',' << cell.metrics.annualised_turnover << ',' << cell.metrics.cost_drag
                    << ',' << cell.rebalances << ',' << cell.metrics.final_equity << '\n';
            }
        }

        // Sharpe is scale-invariant, so the volatility target moves the
        // result only through costs. Print the lookback dimension, which is
        // the one carrying information.
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "net Sharpe (no risk-free), by lookback and volatility target\n\n";
        std::cout << std::setw(10) << "lookback";
        for (double target : targets) {
            std::cout << std::setw(10) << target;
        }
        // Turnover is quoted at the baseline target only. It scales almost
        // linearly with the target -- twice the position is twice the trade --
        // so a single column would otherwise silently report whichever target
        // happened to be last in the row.
        std::cout << std::setw(16) << "turnover@0.10" << '\n';
        std::cout << std::string(10 + 10 * targets.size() + 16, '-') << '\n';

        for (std::size_t lookback : lookbacks) {
            std::cout << std::setw(10) << lookback;
            double turnover = 0.0;
            for (double target : targets) {
                for (const Cell& cell : grid) {
                    if (cell.lookback == lookback && cell.volatility_target == target) {
                        std::cout << std::setw(10) << cell.sharpe_no_rf;
                        if (target == 0.10) {
                            turnover = cell.metrics.annualised_turnover;
                        }
                    }
                }
            }
            std::cout << std::setw(16) << turnover << '\n';
        }

        std::cout << "\n  " << grid.size() << " runs written to "
                  << (config.output_dir / "sweep.csv").string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sweep: " << error.what() << '\n';
        return 1;
    }
}
