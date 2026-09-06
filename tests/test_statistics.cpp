#include <gtest/gtest.h>

#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "backtest/statistics.hpp"

using namespace backtest;

// ---------------------------------------------------------------------------
// Welford
// ---------------------------------------------------------------------------

TEST(Welford, MatchesHandComputedMeanAndVariance) {
    // {2, 4, 4, 4, 5, 5, 7, 9}: mean 5, deviations -3,-1,-1,-1,0,0,2,4,
    // squared sum 9+1+1+1+0+0+4+16 = 32, sample variance 32/7.
    const std::vector<double> xs{2, 4, 4, 4, 5, 5, 7, 9};

    WelfordAccumulator acc;
    for (double x : xs) {
        acc.update(x);
    }

    const double expected_mean = std::accumulate(xs.begin(), xs.end(), 0.0) / xs.size();

    EXPECT_EQ(acc.count(), xs.size());
    EXPECT_NEAR(acc.mean(), 5.0, 1e-12);
    EXPECT_NEAR(acc.mean(), expected_mean, 1e-12);
    EXPECT_NEAR(acc.variance(), 32.0 / 7.0, 1e-12);
    EXPECT_NEAR(acc.stddev(), std::sqrt(32.0 / 7.0), 1e-12);
}

TEST(Welford, SurvivesCatastrophicCancellation) {
    // The spread is 30 but the values sit at 1e9, so sum(x^2) is ~4e18 where
    // the granularity of a double is already in the hundreds. The naive
    // sum-of-squares formula differences two near-equal huge numbers here and
    // returns garbage; Welford stays on the scale of the deviations.
    const std::vector<double> xs{1e9 + 4, 1e9 + 7, 1e9 + 13, 1e9 + 16};

    WelfordAccumulator acc;
    for (double x : xs) {
        acc.update(x);
    }

    // Deviations from the mean 1e9 + 10 are -6, -3, 3, 6 -> 90 / 3 = 30.
    EXPECT_NEAR(acc.mean(), 1e9 + 10.0, 1e-6);
    EXPECT_NEAR(acc.variance(), 30.0, 1e-9);
}

TEST(Welford, VarianceIsNotDefinedBelowTwoObservations) {
    WelfordAccumulator acc;
    EXPECT_TRUE(std::isnan(acc.variance()));

    acc.update(42.0);
    EXPECT_EQ(acc.count(), 1u);
    EXPECT_DOUBLE_EQ(acc.mean(), 42.0);
    EXPECT_TRUE(std::isnan(acc.variance()));

    acc.update(44.0);
    EXPECT_DOUBLE_EQ(acc.mean(), 43.0);
    EXPECT_DOUBLE_EQ(acc.variance(), 2.0);  // (1 + 1) / 1
}

TEST(Welford, ConstantSeriesHasZeroVariance) {
    WelfordAccumulator acc;
    for (int i = 0; i < 100; ++i) {
        acc.update(7.5);
    }
    EXPECT_DOUBLE_EQ(acc.mean(), 7.5);
    EXPECT_NEAR(acc.variance(), 0.0, 1e-18);
}

TEST(Welford, ResetClearsState) {
    WelfordAccumulator acc;
    acc.update(1.0);
    acc.update(100.0);
    acc.reset();
    EXPECT_EQ(acc.count(), 0u);
    EXPECT_DOUBLE_EQ(acc.mean(), 0.0);
    EXPECT_TRUE(std::isnan(acc.variance()));
}

// ---------------------------------------------------------------------------
// EWMA volatility
// ---------------------------------------------------------------------------

TEST(EwmaVolatility, HalflifeOneReproducesHandComputedRecursion) {
    // halflife 1 -> lambda = exp(-ln2) = 0.5 exactly.
    // Seeded from a single observation, so var_1 = r_1^2 and thereafter
    //     var_t = 0.5 * var_{t-1} + 0.5 * r_t^2.
    EwmaVolatility vol(/*halflife=*/1.0, /*seed_count=*/1);
    EXPECT_NEAR(vol.lambda(), 0.5, 1e-15);

    vol.update(0.01);
    ASSERT_TRUE(vol.is_warmed_up());
    const double var1 = 1e-4;
    EXPECT_NEAR(vol.variance(), var1, 1e-18);

    vol.update(-0.02);
    const double var2 = 0.5 * var1 + 0.5 * 4e-4;  // 2.5e-4
    EXPECT_NEAR(vol.variance(), var2, 1e-18);

    vol.update(0.03);
    const double var3 = 0.5 * var2 + 0.5 * 9e-4;  // 5.75e-4
    EXPECT_NEAR(vol.variance(), var3, 1e-18);
    EXPECT_NEAR(vol.value(), std::sqrt(var3), 1e-15);
}

TEST(EwmaVolatility, ConvergesToConstantReturnMagnitude) {
    // Alternating +/-1% has constant squared return, so the estimate should
    // sit exactly on 1% regardless of halflife.
    EwmaVolatility vol(/*halflife=*/60.0, /*seed_count=*/20);
    for (int i = 0; i < 2000; ++i) {
        vol.update(i % 2 == 0 ? 0.01 : -0.01);
    }
    ASSERT_TRUE(vol.is_warmed_up());
    EXPECT_NEAR(vol.value(), 0.01, 1e-12);
    EXPECT_NEAR(vol.annualised(252.0), 0.01 * std::sqrt(252.0), 1e-12);
}

TEST(EwmaVolatility, DecaysTowardsANewLevelAtTheStatedHalflife) {
    // Seed on 1% vol, then feed 2% vol. After exactly one halflife of updates
    // the variance should have closed half the gap between 1e-4 and 4e-4.
    constexpr double kHalflife = 30.0;
    EwmaVolatility vol(kHalflife, /*seed_count=*/1);
    vol.update(0.01);
    ASSERT_NEAR(vol.variance(), 1e-4, 1e-18);

    for (int i = 0; i < static_cast<int>(kHalflife); ++i) {
        vol.update(0.02);
    }
    EXPECT_NEAR(vol.variance(), 0.5 * (1e-4 + 4e-4), 1e-6 * 4e-4);
}

TEST(EwmaVolatility, IsNotUsableBeforeSeedingCompletes) {
    EwmaVolatility vol(/*halflife=*/10.0, /*seed_count=*/5);
    for (int i = 0; i < 4; ++i) {
        vol.update(0.01);
        EXPECT_FALSE(vol.is_warmed_up());
        EXPECT_TRUE(std::isnan(vol.variance()));
        EXPECT_TRUE(std::isnan(vol.value()));
    }
    vol.update(0.01);
    EXPECT_TRUE(vol.is_warmed_up());
    EXPECT_NEAR(vol.value(), 0.01, 1e-15);
}

TEST(EwmaVolatility, SeedsFromTheMeanSquaredReturnNotFromZero) {
    // Seeding at zero would bias the first estimates downward for several
    // halflives, which is precisely when a strategy would be sizing blind.
    EwmaVolatility vol(/*halflife=*/60.0, /*seed_count=*/3);
    vol.update(0.01);
    vol.update(0.02);
    vol.update(0.03);
    ASSERT_TRUE(vol.is_warmed_up());
    EXPECT_NEAR(vol.variance(), (1e-4 + 4e-4 + 9e-4) / 3.0, 1e-18);
}

TEST(EwmaVolatility, RejectsInvalidParameters) {
    EXPECT_THROW(EwmaVolatility(0.0), std::invalid_argument);
    EXPECT_THROW(EwmaVolatility(-1.0), std::invalid_argument);
    EXPECT_THROW(EwmaVolatility(10.0, 0), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// RollingWindow
// ---------------------------------------------------------------------------

TEST(RollingWindow, IndexesFromTheBackBeforeItIsFull) {
    RollingWindow<int> window(5);
    EXPECT_TRUE(window.empty());

    window.push(1);
    window.push(2);
    window.push(3);

    EXPECT_EQ(window.size(), 3u);
    EXPECT_EQ(window.capacity(), 5u);
    EXPECT_FALSE(window.full());
    EXPECT_EQ(window.newest(), 3);
    EXPECT_EQ(window.back(0), 3);
    EXPECT_EQ(window.back(1), 2);
    EXPECT_EQ(window.back(2), 1);
    EXPECT_EQ(window.oldest(), 1);
    EXPECT_THROW(window.back(3), std::out_of_range);
}

TEST(RollingWindow, WrapsAndDropsTheOldestOnOverflow) {
    RollingWindow<int> window(3);
    for (int i = 1; i <= 5; ++i) {
        window.push(i);
    }

    EXPECT_TRUE(window.full());
    EXPECT_EQ(window.size(), 3u);
    EXPECT_EQ(window.back(0), 5);
    EXPECT_EQ(window.back(1), 4);
    EXPECT_EQ(window.back(2), 3);
    EXPECT_EQ(window.oldest(), 3);
    EXPECT_THROW(window.back(3), std::out_of_range);
}

TEST(RollingWindow, SurvivesManyWrapsWithoutDrift) {
    // The momentum lookback indexes this window thousands of times over a
    // backtest; an off-by-one in the modular arithmetic would only surface
    // after several wraps.
    RollingWindow<int> window(252);
    for (int i = 0; i < 10000; ++i) {
        window.push(i);
    }
    EXPECT_EQ(window.newest(), 9999);
    EXPECT_EQ(window.back(251), 9999 - 251);
    EXPECT_EQ(window.oldest(), 9999 - 251);
    for (std::size_t offset = 0; offset < window.size(); ++offset) {
        EXPECT_EQ(window.back(offset), 9999 - static_cast<int>(offset));
    }
}

TEST(RollingWindow, ClearResetsToEmpty) {
    RollingWindow<double> window(4);
    window.push(1.0);
    window.push(2.0);
    window.clear();
    EXPECT_TRUE(window.empty());
    EXPECT_EQ(window.size(), 0u);
    EXPECT_THROW(window.back(0), std::out_of_range);

    window.push(9.0);
    EXPECT_EQ(window.newest(), 9.0);
}

TEST(RollingWindow, RejectsZeroCapacity) {
    EXPECT_THROW(RollingWindow<int>(0), std::invalid_argument);
}
