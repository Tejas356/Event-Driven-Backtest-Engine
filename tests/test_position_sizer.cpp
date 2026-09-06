#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "backtest/engine.hpp"
#include "backtest/metrics.hpp"
#include "position_sizer.hpp"
#include "tsmom.hpp"

using namespace backtest;

namespace {

double gross_exposure(const std::vector<SizedPosition>& positions) {
    double gross = 0.0;
    for (const SizedPosition& position : positions) {
        gross += std::abs(position.weight);
    }
    return gross;
}

}  // namespace

TEST(PositionSizer, WeightsAreInverselyProportionalToVolatility) {
    // The acceptance criterion: 10% and 20% volatility receive weights in a
    // 2:1 ratio, so each contributes the same risk.
    PositionSizer sizer;
    const std::vector<SizedPosition> positions =
        sizer.size({{"CALM", 1.0, 0.10}, {"WILD", 1.0, 0.20}});

    ASSERT_EQ(positions.size(), 2u);
    EXPECT_GT(positions[0].weight, 0.0);
    EXPECT_NEAR(positions[0].weight / positions[1].weight, 2.0, 1e-12);

    // Absolute values too: target 10% vol, two active symbols, so each gets
    // (0.10 / sigma) * 0.5.
    EXPECT_NEAR(positions[0].weight, (0.10 / 0.10) * 0.5, 1e-12);
    EXPECT_NEAR(positions[1].weight, (0.10 / 0.20) * 0.5, 1e-12);
}

TEST(PositionSizer, DirectionSetsTheSignAndZeroMeansNoPosition) {
    PositionSizer sizer;
    const std::vector<SizedPosition> positions =
        sizer.size({{"LONG", 1.0, 0.10}, {"SHORT", -1.0, 0.10}, {"FLAT", 0.0, 0.10}});

    EXPECT_GT(positions[0].weight, 0.0);
    EXPECT_LT(positions[1].weight, 0.0);
    EXPECT_DOUBLE_EQ(positions[2].weight, 0.0);
    // Equal and opposite, since the volatilities match.
    EXPECT_NEAR(positions[0].weight, -positions[1].weight, 1e-15);
}

TEST(PositionSizer, AFlatSignalDoesNotShrinkTheOtherPositions) {
    // N counts active signals only. Were the flat symbol included in the
    // divisor, adding an asset with no view would quietly de-risk the book.
    PositionSizer sizer;
    const std::vector<SizedPosition> two = sizer.size({{"A", 1.0, 0.10}, {"B", 1.0, 0.10}});
    const std::vector<SizedPosition> two_plus_flat =
        sizer.size({{"A", 1.0, 0.10}, {"B", 1.0, 0.10}, {"C", 0.0, 0.10}});

    EXPECT_DOUBLE_EQ(two[0].weight, two_plus_flat[0].weight);
    EXPECT_DOUBLE_EQ(two[1].weight, two_plus_flat[1].weight);
}

TEST(PositionSizer, LeverageCapScalesGrossExposureToExactlyTheLimit) {
    // Four very quiet assets would each want a large position; together they
    // breach the cap and must be scaled back to exactly 2.0.
    PositionSizer sizer{SizingConfig{.volatility_target = 0.10, .max_gross_leverage = 2.0}};
    const std::vector<SizedPosition> positions = sizer.size({{"A", 1.0, 0.02},
                                                             {"B", -1.0, 0.02},
                                                             {"C", 1.0, 0.02},
                                                             {"D", -1.0, 0.02}});

    EXPECT_NEAR(gross_exposure(positions), 2.0, 1e-12);
    // Scaled proportionally, so the relative allocation is untouched: all
    // four had equal volatility and remain equal in magnitude.
    for (const SizedPosition& position : positions) {
        EXPECT_NEAR(std::abs(position.weight), 0.5, 1e-12);
    }
}

TEST(PositionSizer, TheCapPreservesRelativeSizingRatherThanTruncating) {
    PositionSizer sizer{SizingConfig{.volatility_target = 0.10, .max_gross_leverage = 1.0}};
    const std::vector<SizedPosition> positions =
        sizer.size({{"CALM", 1.0, 0.01}, {"WILD", 1.0, 0.02}});

    EXPECT_NEAR(gross_exposure(positions), 1.0, 1e-12);
    // Still 2:1 after scaling. Truncating the larger position instead would
    // have destroyed the equal-risk property the sizing rule exists for.
    EXPECT_NEAR(positions[0].weight / positions[1].weight, 2.0, 1e-12);
}

TEST(PositionSizer, LeavesGrossExposureAloneWhenItIsInsideTheCap) {
    PositionSizer sizer{SizingConfig{.volatility_target = 0.10, .max_gross_leverage = 2.0}};
    const std::vector<SizedPosition> positions =
        sizer.size({{"A", 1.0, 0.20}, {"B", -1.0, 0.20}});

    // (0.10/0.20) * 0.5 each, so gross is 0.5 -- well inside the cap and
    // therefore untouched.
    EXPECT_NEAR(gross_exposure(positions), 0.5, 1e-12);
}

TEST(PositionSizer, RefusesToInvertAnUnusableVolatilityEstimate) {
    // Dividing by a near-zero volatility is how a sizing rule turns a quiet
    // asset into an enormous position.
    PositionSizer sizer;
    const std::vector<SizedPosition> positions = sizer.size({{"OK", 1.0, 0.10},
                                                             {"ZERO", 1.0, 0.0},
                                                             {"NAN", 1.0, std::nan("")},
                                                             {"NEG", 1.0, -0.10}});

    EXPECT_GT(positions[0].weight, 0.0);
    EXPECT_DOUBLE_EQ(positions[1].weight, 0.0);
    EXPECT_DOUBLE_EQ(positions[2].weight, 0.0);
    EXPECT_DOUBLE_EQ(positions[3].weight, 0.0);
    // Only the usable symbol counted towards N, so it gets the full share.
    EXPECT_NEAR(positions[0].weight, 1.0, 1e-12);
}

TEST(PositionSizer, NoActiveSignalsMeansAFlatBook) {
    PositionSizer sizer;
    const std::vector<SizedPosition> positions =
        sizer.size({{"A", 0.0, 0.10}, {"B", 0.0, 0.10}});
    ASSERT_EQ(positions.size(), 2u);
    EXPECT_DOUBLE_EQ(gross_exposure(positions), 0.0);
    EXPECT_TRUE(sizer.size({}).empty());
}

TEST(PositionSizer, RejectsInvalidConfiguration) {
    EXPECT_THROW(PositionSizer{SizingConfig{.volatility_target = 0.0}}, std::invalid_argument);
    EXPECT_THROW(PositionSizer{SizingConfig{.max_gross_leverage = 0.0}}, std::invalid_argument);
}

// --------------------------------------------------------------------------
// Realised volatility of the whole book, on real data
// --------------------------------------------------------------------------

TEST(TsmomSizing, RealisedVolatilityLandsNearTheTarget) {
    // The acceptance criterion that only a full run can check.
    //
    // The target is 10% annualised *per position*, and each weight is then
    // scaled by 1/N. Eight positions each contributing 10%/8 of risk sum to
    // 10%/sqrt(8) = 3.5% if they are uncorrelated, and somewhat more than
    // that in practice since trend positions across asset classes are not
    // independent. It is the portfolio-level figure that a run can measure,
    // so that is what is checked here -- comparing realised portfolio
    // volatility against the undiversified 10% would be comparing against a
    // number the sizing rule never aimed at.
    //
    // It will not be exact: volatility forecasts are imperfect and
    // correlations shift, so a rule that sizes one asset at a time cannot
    // hit a portfolio target. A large miss means a bug.
    const std::filesystem::path data_dir{TSMOM_DATA_DIR};
    const std::vector<Symbol> universe{"DBC", "EEM", "EFA", "GLD", "IEF", "SPY", "TLT", "UUP"};
    for (const Symbol& symbol : universe) {
        if (!std::filesystem::exists(data_dir / (symbol + ".csv"))) {
            GTEST_SKIP() << "no CSVs in " << data_dir << "; run python data/fetch_data.py";
        }
    }

    DataHandler data = DataHandler::from_csv_directory(data_dir, universe);
    TsmomStrategy strategy{universe};
    Portfolio portfolio{PortfolioConfig{.initial_capital = 1000000.0}};
    // Costs off: this criterion is about the sizing rule, and a cost model
    // would only blur what is being measured.
    ExecutionHandler execution{CostModel{.commission_bps = 0.0, .slippage_bps = 0.0}};

    Engine(data, strategy, portfolio, execution).run();
    const BacktestMetrics metrics = compute_metrics(portfolio);

    const double portfolio_target = 0.10 / std::sqrt(static_cast<double>(universe.size()));
    EXPECT_NEAR(metrics.net.annualised_volatility, portfolio_target, 0.03)
        << "realised volatility is more than 3 percentage points from target";

    // And the diversification is real: the book runs at well under the
    // per-position target because the positions do not all move together.
    EXPECT_LT(metrics.net.annualised_volatility, 0.10);

    // The leverage cap must hold on every single day, not just on average.
    for (const EquityPoint& point : portfolio.equity_curve()) {
        ASSERT_LE(point.gross_exposure, 2.0 + 1e-9)
            << "gross exposure breached the cap on " << point.ts;
    }
}
