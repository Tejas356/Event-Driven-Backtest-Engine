#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "backtest/metrics.hpp"

using namespace backtest;
using namespace std::chrono;

namespace {

// Consecutive calendar days from 2007-01-01, so that drawdown durations can
// be reasoned about without a trading calendar getting in the way.
std::vector<Timestamp> consecutive_days(std::size_t count) {
    std::vector<Timestamp> dates;
    dates.reserve(count);
    const Timestamp start{2007y / January / 1};
    for (std::size_t i = 0; i < count; ++i) {
        dates.push_back(start + days{static_cast<int>(i)});
    }
    return dates;
}

}  // namespace

TEST(Metrics, ReturnVolatilityAndSharpeMatchHandComputedValues) {
    // Returns of exactly +10%, -10%.
    const std::vector<double> equity{100.0, 110.0, 99.0};

    const PerformanceMetrics metrics =
        compute_performance(equity, consecutive_days(3), MetricsConfig{.risk_free_rate = 0.0});

    EXPECT_EQ(metrics.observations, 2u);

    // Sample stdev of {0.1, -0.1} is sqrt(0.02).
    EXPECT_NEAR(metrics.annualised_volatility, std::sqrt(0.02) * std::sqrt(252.0), 1e-12);
    // Mean return is exactly zero, so with no risk-free rate the Sharpe is
    // zero too -- despite the curve having lost money, which is the point:
    // the arithmetic mean of a symmetric pair is not the compound return.
    EXPECT_NEAR(metrics.sharpe, 0.0, 1e-12);
    // Two days of returns is 2/252 of a year.
    EXPECT_NEAR(metrics.annualised_return, std::pow(0.99, 126.0) - 1.0, 1e-12);
}

TEST(Metrics, RiskFreeRateIsSubtractedFromTheNumeratorOnly) {
    const std::vector<double> equity{100.0, 101.0, 102.01};  // +1% twice
    const std::vector<Timestamp> dates = consecutive_days(3);

    const PerformanceMetrics zero_rf =
        compute_performance(equity, dates, MetricsConfig{.risk_free_rate = 0.0});
    const PerformanceMetrics with_rf =
        compute_performance(equity, dates, MetricsConfig{.risk_free_rate = 0.02});

    // Both returns are identical, so the denominator is zero and the Sharpe
    // is reported as zero rather than as an infinity.
    EXPECT_NEAR(zero_rf.annualised_volatility, 0.0, 1e-15);
    EXPECT_DOUBLE_EQ(zero_rf.sharpe, 0.0);
    EXPECT_DOUBLE_EQ(with_rf.sharpe, 0.0);
}

// The acceptance criterion for Step 8: a drawdown of exactly 20% lasting
// exactly 30 days.
//
// Equity holds at 100 through day 10, which is where the peak is set. It
// falls to 80 -- a 20% drawdown to the basis point -- and regains 100 on day
// 40. Peak to recovery is 40 - 10 = 30 calendar days.
TEST(Metrics, DrawdownDepthAndDurationAreBothExact) {
    std::vector<double> equity;
    for (int day = 0; day <= 10; ++day) {
        equity.push_back(100.0);
    }
    for (int day = 11; day <= 39; ++day) {
        equity.push_back(80.0);
    }
    equity.push_back(100.0);  // day 40, recovery
    ASSERT_EQ(equity.size(), 41u);

    const PerformanceMetrics metrics = compute_performance(equity, consecutive_days(41));

    EXPECT_NEAR(metrics.max_drawdown, 0.20, 1e-15);
    EXPECT_EQ(metrics.max_drawdown_days, 30);
}

TEST(Metrics, AnUnrecoveredDrawdownIsMeasuredToTheEndOfTheSample) {
    // Never recovering is the worst case, so it must not be discarded for
    // lacking a recovery date.
    std::vector<double> equity{100.0};
    for (int day = 1; day <= 20; ++day) {
        equity.push_back(70.0);
    }

    const PerformanceMetrics metrics = compute_performance(equity, consecutive_days(21));
    EXPECT_NEAR(metrics.max_drawdown, 0.30, 1e-15);
    EXPECT_EQ(metrics.max_drawdown_days, 20);
}

TEST(Metrics, DrawdownIsZeroForAMonotonicallyRisingCurve) {
    const std::vector<double> equity{100.0, 101.0, 102.0, 103.0};
    const PerformanceMetrics metrics = compute_performance(equity, consecutive_days(4));
    EXPECT_DOUBLE_EQ(metrics.max_drawdown, 0.0);
    EXPECT_EQ(metrics.max_drawdown_days, 0);
}

TEST(Metrics, SkewnessAndExcessKurtosisMatchHandComputedValues) {
    // Returns alternate exactly +10%, -10%, +10%, -10%.
    const std::vector<double> equity{100.0, 110.0, 99.0, 108.9, 98.01};

    const PerformanceMetrics metrics = compute_performance(equity, consecutive_days(5));

    ASSERT_EQ(metrics.observations, 4u);
    // Symmetric about zero, so the third moment vanishes.
    EXPECT_NEAR(metrics.skewness, 0.0, 1e-12);
    // A two-point distribution has population kurtosis of exactly 1, so the
    // excess is exactly -2.
    EXPECT_NEAR(metrics.excess_kurtosis, -2.0, 1e-10);
}

TEST(Metrics, SkewnessPicksUpAsymmetryWithTheRightSign) {
    // Three small losses and one large gain: right-skewed, which is the
    // shape trend following is supposed to produce.
    std::vector<double> equity{100.0};
    for (double ret : {-0.01, -0.01, -0.01, 0.05}) {
        equity.push_back(equity.back() * (1.0 + ret));
    }

    const PerformanceMetrics metrics = compute_performance(equity, consecutive_days(5));

    EXPECT_GT(metrics.skewness, 0.0);
    // For three values at -x and one at +3x the population skew is sqrt(4/3).
    EXPECT_NEAR(metrics.skewness, std::sqrt(4.0 / 3.0), 1e-9);
}

TEST(Metrics, RejectsMismatchedInputLengths) {
    EXPECT_THROW(compute_performance({1.0, 2.0}, consecutive_days(3)), std::invalid_argument);
}

TEST(Metrics, DegenerateCurvesReportZeroesRatherThanNonsense) {
    EXPECT_EQ(compute_performance({}, {}).observations, 0u);
    EXPECT_EQ(compute_performance({100.0}, consecutive_days(1)).observations, 0u);
    EXPECT_EQ(compute_performance({100.0, 100.0}, consecutive_days(2)).observations, 1u);
}

// --------------------------------------------------------------------------
// Whole-run metrics: turnover, and gross beside net
// --------------------------------------------------------------------------

namespace {

MarketEvent flat_market(Timestamp ts, const Symbol& symbol, double price) {
    return MarketEvent{ts, symbol,
                       Bar{.ts = ts,
                           .open = price,
                           .high = price,
                           .low = price,
                           .close = price,
                           .volume = 1000}};
}

}  // namespace

TEST(Metrics, AnnualisedTurnoverMatchesTheHandComputedRatio) {
    // A round trip of 10,000 notional each way on a 100,000 book held flat
    // for 252 days: 20,000 of traded notional over an average equity of
    // 100,000, across 251/252 of a year.
    Portfolio portfolio{PortfolioConfig{.initial_capital = 100000.0}};
    const std::vector<Timestamp> dates = consecutive_days(252);

    portfolio.mark_to_market(flat_market(dates[0], "AAA", 100.0));
    portfolio.on_fill(FillEvent{dates[0], "AAA", 100.0, 100.0, 0.0, 0.0});
    portfolio.record_equity(dates[0]);

    portfolio.on_fill(FillEvent{dates[1], "AAA", -100.0, 100.0, 0.0, 0.0});
    for (std::size_t i = 1; i < dates.size(); ++i) {
        portfolio.mark_to_market(flat_market(dates[i], "AAA", 100.0));
        portfolio.record_equity(dates[i]);
    }

    const BacktestMetrics metrics = compute_metrics(portfolio);

    EXPECT_DOUBLE_EQ(portfolio.total_traded_notional(), 20000.0);
    EXPECT_NEAR(metrics.years, 251.0 / 252.0, 1e-12);
    EXPECT_NEAR(metrics.annualised_turnover, 0.2 * 252.0 / 251.0, 1e-9);
}

TEST(Metrics, GrossSitsAboveNetByExactlyTheCostsCharged) {
    Portfolio portfolio{PortfolioConfig{.initial_capital = 100000.0}};
    const std::vector<Timestamp> dates = consecutive_days(10);

    portfolio.mark_to_market(flat_market(dates[0], "AAA", 100.0));
    portfolio.record_equity(dates[0]);

    // Buy 100 shares at 100 paying 25 of commission and 25 of slippage.
    portfolio.on_fill(FillEvent{dates[1], "AAA", 100.0, 100.0, 25.0, 25.0});
    for (std::size_t i = 1; i < dates.size(); ++i) {
        portfolio.mark_to_market(flat_market(dates[i], "AAA", 100.0 + static_cast<double>(i)));
        portfolio.record_equity(dates[i]);
    }

    const BacktestMetrics metrics = compute_metrics(portfolio);

    EXPECT_DOUBLE_EQ(metrics.total_commission, 25.0);
    EXPECT_DOUBLE_EQ(metrics.total_slippage, 25.0);
    // The whole point of the module: the gap is visible, and it is the cost.
    EXPECT_NEAR(metrics.gross.annualised_return - metrics.net.annualised_return,
                metrics.cost_drag, 1e-15);
    EXPECT_GT(metrics.gross.annualised_return, metrics.net.annualised_return);
    EXPECT_GT(metrics.gross.sharpe, metrics.net.sharpe);
    EXPECT_DOUBLE_EQ(metrics.initial_equity, 100000.0);
}

TEST(Metrics, WithoutCostsGrossAndNetAreIdentical) {
    Portfolio portfolio{PortfolioConfig{.initial_capital = 100000.0}};
    const std::vector<Timestamp> dates = consecutive_days(10);
    for (std::size_t i = 0; i < dates.size(); ++i) {
        portfolio.mark_to_market(flat_market(dates[i], "AAA", 100.0 + static_cast<double>(i)));
        portfolio.record_equity(dates[i]);
    }

    const BacktestMetrics metrics = compute_metrics(portfolio);
    EXPECT_DOUBLE_EQ(metrics.cost_drag, 0.0);
    EXPECT_DOUBLE_EQ(metrics.gross.sharpe, metrics.net.sharpe);
    EXPECT_DOUBLE_EQ(metrics.annualised_turnover, 0.0);
}
