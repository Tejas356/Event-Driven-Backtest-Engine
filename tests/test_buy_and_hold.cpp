// Step 6 of the build plan exists so that nothing downstream is trusted
// before the harness is checked against an answer that is already known.
//
// The real-data tests here are skipped rather than failed when the CSVs have
// not been downloaded, so a fresh clone still runs green; the synthetic tests
// above them always run.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <map>
#include <vector>

#include "backtest/engine.hpp"
#include "backtest/summary.hpp"
#include "buy_and_hold.hpp"

using namespace backtest;
using namespace std::chrono;

namespace {

const std::filesystem::path kDataDir{TSMOM_DATA_DIR};

bool spy_data_available() {
    return std::filesystem::exists(kDataDir / "SPY.csv");
}

Bar bar_on(int y, unsigned m, unsigned d, double open, double close) {
    return Bar{.ts = Timestamp{year{y} / month{m} / day{d}},
               .open = open,
               .high = std::max(open, close),
               .low = std::min(open, close),
               .close = close,
               .volume = 1000};
}

}  // namespace

TEST(BuyAndHold, BuysOnceAndNeverTradesAgain) {
    std::map<Symbol, std::vector<Bar>> bars{
        {"AAA",
         {bar_on(2007, 1, 2, 100.0, 100.0), bar_on(2007, 1, 3, 100.0, 110.0),
          bar_on(2007, 1, 4, 110.0, 90.0), bar_on(2007, 1, 5, 90.0, 120.0)}}};

    DataHandler data = DataHandler::from_bars(bars);
    BuyAndHoldStrategy strategy{"AAA"};
    Portfolio portfolio{PortfolioConfig{.initial_capital = 100000.0}};
    ExecutionHandler execution{CostModel{.commission_bps = 0.0, .slippage_bps = 0.0}};

    const RunStatistics stats = Engine(data, strategy, portfolio, execution).run();

    // One signal, one order, one fill -- despite the price swinging enough
    // that a rebalancing strategy would have traded repeatedly.
    EXPECT_EQ(stats.signals, 1u);
    EXPECT_EQ(stats.fills, 1u);
    EXPECT_DOUBLE_EQ(portfolio.total_costs(), 0.0);

    // Entered at the 3 Jan open of 100 with 100000/100 = 1000 shares, so the
    // book is worth 1000 * 120 at the end and cash is zero.
    EXPECT_DOUBLE_EQ(portfolio.position("AAA"), 1000.0);
    EXPECT_NEAR(portfolio.cash(), 0.0, 1e-9);
    EXPECT_NEAR(portfolio.equity_curve().back().equity, 120000.0, 1e-9);
}

TEST(Summarise, MatchesHandComputedStatisticsOnAShortCurve) {
    // Equity 100, 110, 99: returns +10% then -10%.
    const std::vector<EquityPoint> curve{
        {.ts = Timestamp{2007y / January / 2}, .equity = 100.0},
        {.ts = Timestamp{2007y / January / 3}, .equity = 110.0},
        {.ts = Timestamp{2007y / January / 4}, .equity = 99.0}};

    const SummaryStatistics stats = summarise(curve, /*risk_free_rate=*/0.0);

    EXPECT_EQ(stats.observations, 2u);
    // Sample stdev of {0.1, -0.1} is sqrt(0.02) = 0.1414...
    const double expected_stdev = std::sqrt(0.02);
    EXPECT_NEAR(stats.annualised_volatility, expected_stdev * std::sqrt(252.0), 1e-12);
    // Mean excess return is exactly zero, so the Sharpe is too.
    EXPECT_NEAR(stats.sharpe, 0.0, 1e-12);
    // Peak 110, trough 99: an 11/110 = 10% drawdown.
    EXPECT_NEAR(stats.max_drawdown, 0.1, 1e-12);
    // (99/100)^(252/2) - 1
    EXPECT_NEAR(stats.annualised_return, std::pow(0.99, 126.0) - 1.0, 1e-12);
}

TEST(Summarise, ReturnsZeroesRatherThanNonsenseOnADegenerateCurve) {
    EXPECT_EQ(summarise({}).observations, 0u);
    EXPECT_EQ(summarise({{.ts = Timestamp{2007y / January / 2}, .equity = 100.0}}).observations,
              0u);
}

// --------------------------------------------------------------------------
// Validation against real SPY data
// --------------------------------------------------------------------------

TEST(HarnessValidation, BuyAndHoldSpyReproducesTheKnownStatistics) {
    if (!spy_data_available()) {
        GTEST_SKIP() << "no SPY.csv in " << kDataDir << "; run python data/fetch_data.py";
    }

    DataHandler data = DataHandler::from_csv_directory(kDataDir, {"SPY"});
    BuyAndHoldStrategy strategy{"SPY"};
    Portfolio portfolio{PortfolioConfig{.initial_capital = 1000000.0}};
    ExecutionHandler execution{CostModel{.commission_bps = 0.0, .slippage_bps = 0.0}};

    const RunStatistics run = Engine(data, strategy, portfolio, execution).run();
    const SummaryStatistics stats = summarise(portfolio.equity_curve());

    ASSERT_GT(run.timestamps, 4000u) << "expected a long sample starting in 2007";
    EXPECT_EQ(portfolio.equity_curve().size(), run.timestamps);
    EXPECT_EQ(run.fills, 1u);

    // The calibration the spec gives for a 2007-onwards window.
    EXPECT_GE(stats.sharpe, 0.4);
    EXPECT_LE(stats.sharpe, 0.6);
    EXPECT_GE(stats.annualised_volatility, 0.15);
    EXPECT_LE(stats.annualised_volatility, 0.25);

    // If the financial crisis is not visible in the equity curve, something
    // is wrong -- this is the single most recognisable feature of the sample.
    EXPECT_GE(stats.max_drawdown, 0.50);
    EXPECT_LE(stats.max_drawdown, 0.60);
}

TEST(HarnessValidation, TheDrawdownTroughFallsInTheFinancialCrisis) {
    if (!spy_data_available()) {
        GTEST_SKIP() << "no SPY.csv in " << kDataDir << "; run python data/fetch_data.py";
    }

    DataHandler data = DataHandler::from_csv_directory(kDataDir, {"SPY"});
    BuyAndHoldStrategy strategy{"SPY"};
    Portfolio portfolio{PortfolioConfig{.initial_capital = 1000000.0}};
    ExecutionHandler execution{CostModel{.commission_bps = 0.0, .slippage_bps = 0.0}};
    Engine(data, strategy, portfolio, execution).run();

    // A drawdown of the right size at the wrong date would still be a bug,
    // so pin the trough as well as the depth. SPY bottomed on 2009-03-09.
    double peak = 0.0;
    double worst = 0.0;
    Timestamp trough{};
    for (const EquityPoint& point : portfolio.equity_curve()) {
        peak = std::max(peak, point.equity);
        const double drawdown = (peak - point.equity) / peak;
        if (drawdown > worst) {
            worst = drawdown;
            trough = point.ts;
        }
    }

    const year_month_day ymd{trough};
    EXPECT_EQ(ymd.year(), 2009y) << "the deepest drawdown should bottom in 2009";
    EXPECT_EQ(ymd.month(), March) << "SPY bottomed on 2009-03-09";
}
