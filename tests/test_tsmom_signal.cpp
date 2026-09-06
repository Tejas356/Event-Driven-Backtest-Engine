#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <vector>

#include "tsmom_signal.hpp"

using namespace backtest;
using namespace std::chrono;

namespace {

// A series of `count` bars whose close follows the given rule.
template <typename Rule>
std::vector<Bar> series(std::size_t count, Rule rule) {
    std::vector<Bar> bars;
    bars.reserve(count);
    const Timestamp start{2007y / January / 1};
    for (std::size_t i = 0; i < count; ++i) {
        const double close = rule(i);
        bars.push_back(Bar{.ts = start + days{static_cast<int>(i)},
                           .open = close,
                           .high = close,
                           .low = close,
                           .close = close,
                           .volume = 1000});
    }
    return bars;
}

}  // namespace

TEST(TsmomSignal, IsLongOnAMonotonicallyRisingSeries) {
    TsmomSignal signal{252};
    const std::vector<Bar> bars = series(300, [](std::size_t i) { return 100.0 + static_cast<double>(i); });

    const std::optional<double> direction = signal.direction(bars);
    ASSERT_TRUE(direction.has_value());
    EXPECT_DOUBLE_EQ(*direction, 1.0);

    // 299 / 47 - 1, comparing the last close against the one 252 bars back.
    const std::optional<double> ret = signal.lookback_return(bars);
    ASSERT_TRUE(ret.has_value());
    EXPECT_NEAR(*ret, 399.0 / 147.0 - 1.0, 1e-12);
}

TEST(TsmomSignal, IsShortOnAMonotonicallyFallingSeries) {
    TsmomSignal signal{252};
    const std::vector<Bar> bars = series(300, [](std::size_t i) { return 500.0 - static_cast<double>(i); });

    const std::optional<double> direction = signal.direction(bars);
    ASSERT_TRUE(direction.has_value());
    EXPECT_DOUBLE_EQ(*direction, -1.0);
}

TEST(TsmomSignal, EmitsNothingBeforeWarmup) {
    TsmomSignal signal{252};
    EXPECT_EQ(signal.warmup_bars(), 253u);

    // One bar short of the requirement: still no signal, not a zero.
    const std::vector<Bar> just_short = series(252, [](std::size_t i) { return 100.0 + static_cast<double>(i); });
    EXPECT_FALSE(signal.direction(just_short).has_value());
    EXPECT_FALSE(signal.lookback_return(just_short).has_value());

    // Exactly enough.
    const std::vector<Bar> just_enough = series(253, [](std::size_t i) { return 100.0 + static_cast<double>(i); });
    EXPECT_TRUE(signal.direction(just_enough).has_value());

    EXPECT_FALSE(signal.direction(std::span<const Bar>{}).has_value());
}

TEST(TsmomSignal, ComparesAgainstExactlyTheBarLookbackDaysBack) {
    // The whole series is flat at 100 except the bar 10 back, which is 50.
    // A correct implementation reads that bar and is long; an off-by-one
    // reads a flat bar and reports zero.
    TsmomSignal signal{10};
    std::vector<Bar> bars = series(20, [](std::size_t) { return 100.0; });
    bars[bars.size() - 1 - 10].close = 50.0;

    const std::optional<double> ret = signal.lookback_return(bars);
    ASSERT_TRUE(ret.has_value());
    EXPECT_NEAR(*ret, 100.0 / 50.0 - 1.0, 1e-12);
    EXPECT_DOUBLE_EQ(*signal.direction(bars), 1.0);
}

TEST(TsmomSignal, ReportsZeroForAnExactlyFlatLookback) {
    TsmomSignal signal{10};
    const std::vector<Bar> bars = series(20, [](std::size_t) { return 100.0; });

    const std::optional<double> direction = signal.direction(bars);
    ASSERT_TRUE(direction.has_value());
    EXPECT_DOUBLE_EQ(*direction, 0.0);
}

TEST(TsmomSignal, ShorterLookbacksRespondToRecentMovesTheLongerOneMisses) {
    // Rises steeply for 200 bars, then drifts down for 100. The recent drift
    // is enough to turn a 21-day lookback negative but nowhere near enough to
    // undo the earlier rise, so the 252-day lookback is still long. Neither
    // is wrong -- that divergence is exactly what the sweep in Step 13
    // measures, and it is why a result holding across neighbouring lookbacks
    // is worth more than a spike at one.
    const std::vector<Bar> bars = series(300, [](std::size_t i) {
        return i < 200 ? 100.0 + static_cast<double>(i)
                       : 300.0 - 0.2 * static_cast<double>(i - 200);
    });

    EXPECT_DOUBLE_EQ(*TsmomSignal{21}.direction(bars), -1.0);
    EXPECT_DOUBLE_EQ(*TsmomSignal{252}.direction(bars), 1.0);
}
