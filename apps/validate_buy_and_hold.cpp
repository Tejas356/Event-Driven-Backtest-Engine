// Runs buy-and-hold SPY through the full engine and writes the equity curve,
// so that analysis/validate.py can recompute the same statistics in pandas
// and compare. An engine that cannot reproduce buy-and-hold cannot be
// trusted on anything harder.

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include "backtest/engine.hpp"
#include "backtest/summary.hpp"
#include "buy_and_hold.hpp"

using namespace backtest;

namespace {

std::string to_iso(Timestamp ts) {
    const std::chrono::year_month_day ymd{ts};
    std::ostringstream out;
    out << std::setfill('0') << static_cast<int>(ymd.year()) << '-' << std::setw(2)
        << static_cast<unsigned>(ymd.month()) << '-' << std::setw(2)
        << static_cast<unsigned>(ymd.day());
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path data_dir = argc > 1 ? argv[1] : TSMOM_DATA_DIR;
    const std::filesystem::path out_dir = argc > 2 ? argv[2] : "results";
    const Symbol symbol = "SPY";

    try {
        DataHandler data = DataHandler::from_csv_directory(data_dir, {symbol});
        BuyAndHoldStrategy strategy{symbol};
        Portfolio portfolio{PortfolioConfig{.initial_capital = 1000000.0}};
        // Costs are switched off deliberately. The point of this run is to
        // check the accounting against a price series, and a cost model the
        // reference implementation would also have to replicate would only
        // blur what is being validated.
        ExecutionHandler execution{CostModel{.commission_bps = 0.0, .slippage_bps = 0.0}};

        const RunStatistics run = Engine(data, strategy, portfolio, execution).run();
        const SummaryStatistics stats = summarise(portfolio.equity_curve());

        std::filesystem::create_directories(out_dir);
        std::ofstream out(out_dir / "buy_and_hold_equity.csv");
        out << "date,equity\n" << std::setprecision(17);
        for (const EquityPoint& point : portfolio.equity_curve()) {
            out << to_iso(point.ts) << ',' << point.equity << '\n';
        }

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "symbol               " << symbol << '\n'
                  << "bars                 " << run.timestamps << '\n'
                  << "fills                " << run.fills << '\n'
                  << "annualised_return    " << stats.annualised_return << '\n'
                  << "annualised_volatility " << stats.annualised_volatility << '\n'
                  << "sharpe               " << stats.sharpe << '\n'
                  << "max_drawdown         " << stats.max_drawdown << '\n'
                  << "final_equity         " << portfolio.equity_curve().back().equity << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "validate_buy_and_hold: " << error.what() << '\n';
        return 1;
    }
}
