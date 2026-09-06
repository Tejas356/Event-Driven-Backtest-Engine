// Properties the parameter sweep depends on. If Sharpe were not
// scale-invariant, or if the lookback did not actually change the positions
// taken, the sweep grid would be measuring something other than robustness.

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <vector>

#include "backtest/engine.hpp"
#include "backtest/metrics.hpp"
#include "tsmom.hpp"

using namespace backtest;

namespace {

const std::filesystem::path kDataDir{TSMOM_DATA_DIR};
const std::vector<Symbol> kUniverse{"DBC", "EEM", "EFA", "GLD", "IEF", "SPY", "TLT", "UUP"};

bool universe_available() {
    for (const Symbol& symbol : kUniverse) {
        if (!std::filesystem::exists(kDataDir / (symbol + ".csv"))) {
            return false;
        }
    }
    return true;
}

BacktestMetrics run(std::size_t lookback, double volatility_target, CostModel costs) {
    DataHandler data = DataHandler::from_csv_directory(kDataDir, kUniverse);
    TsmomStrategy strategy{
        kUniverse,
        TsmomConfig{.lookback = lookback,
                    .sizing = SizingConfig{.volatility_target = volatility_target},
                    .frequency = RebalanceFrequency::MonthEnd}};
    Portfolio portfolio{PortfolioConfig{.initial_capital = 1000000.0}};
    ExecutionHandler execution{costs};
    Engine(data, strategy, portfolio, execution).run();
    return compute_metrics(portfolio);
}

}  // namespace

TEST(Robustness, SharpeIsInvariantToTheVolatilityTargetWithoutCosts) {
    if (!universe_available()) {
        GTEST_SKIP() << "no CSVs in " << kDataDir << "; run python data/fetch_data.py";
    }
    const CostModel free{.commission_bps = 0.0, .slippage_bps = 0.0};

    const BacktestMetrics half = run(252, 0.05, free);
    const BacktestMetrics base = run(252, 0.10, free);

    // Doubling the target doubles every position, so volatility doubles and
    // the ratio is unchanged. This is why the sweep treats the lookback as
    // the dimension carrying information and the target as a scale knob.
    EXPECT_NEAR(base.net.annualised_volatility, 2.0 * half.net.annualised_volatility,
                0.02 * base.net.annualised_volatility);
    // Undone at the rate compute_metrics actually charged, which is the
    // MetricsConfig default of 2%. Comparing the risk-free-adjusted figures
    // directly would not be comparing like with like: the deduction is a
    // fixed rate against a volatility that differs between the two runs.
    constexpr double kRiskFree = 0.02;
    EXPECT_NEAR(sharpe_without_risk_free(base.net, kRiskFree),
                sharpe_without_risk_free(half.net, kRiskFree), 0.05);
}

TEST(Robustness, CostsReduceSharpeAndTheDragScalesWithPositionSize) {
    if (!universe_available()) {
        GTEST_SKIP() << "no CSVs in " << kDataDir << "; run python data/fetch_data.py";
    }

    const BacktestMetrics small = run(252, 0.05, CostModel{});
    const BacktestMetrics large = run(252, 0.20, CostModel{});

    EXPECT_GT(small.gross.sharpe, small.net.sharpe);
    EXPECT_GT(large.gross.sharpe, large.net.sharpe);
    // Four times the position, so roughly four times the traded notional and
    // four times the cost in currency.
    EXPECT_GT(large.total_commission, 3.0 * small.total_commission);
}

TEST(Robustness, DifferentLookbacksProduceDifferentResults) {
    if (!universe_available()) {
        GTEST_SKIP() << "no CSVs in " << kDataDir << "; run python data/fetch_data.py";
    }

    const BacktestMetrics fast = run(63, 0.10, CostModel{});
    const BacktestMetrics slow = run(378, 0.10, CostModel{});

    // A shorter lookback flips sign more often, so it trades more.
    EXPECT_GT(fast.annualised_turnover, slow.annualised_turnover);
    EXPECT_NE(fast.final_equity, slow.final_equity);
}

TEST(Robustness, EveryLookbackOnTheSweepGridProducesAUsableRun) {
    if (!universe_available()) {
        GTEST_SKIP() << "no CSVs in " << kDataDir << "; run python data/fetch_data.py";
    }

    // Not an assertion about the level of the Sharpe -- that would be
    // asserting a result rather than testing code. This checks only that
    // every cell of the grid completes and produces finite numbers, so a
    // sweep cannot quietly report NaN for a corner of the space.
    for (std::size_t lookback : {63u, 126u, 189u, 252u, 378u}) {
        const BacktestMetrics metrics = run(lookback, 0.10, CostModel{});
        EXPECT_GT(metrics.net.observations, 4000u) << "lookback " << lookback;
        EXPECT_TRUE(std::isfinite(metrics.net.sharpe)) << "lookback " << lookback;
        EXPECT_TRUE(std::isfinite(metrics.net.annualised_volatility)) << "lookback " << lookback;
        EXPECT_GT(metrics.net.annualised_volatility, 0.0) << "lookback " << lookback;
        EXPECT_GT(metrics.final_equity, 0.0) << "lookback " << lookback;
    }
}
