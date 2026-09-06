#include <gtest/gtest.h>

#include <chrono>

#include "backtest/types.hpp"

using namespace backtest;
using namespace std::chrono;

TEST(Types, BarConstructionAndFieldAccess) {
    const Bar bar{
        .ts = Timestamp{2007y / January / 3},
        .open = 141.37,
        .high = 141.84,
        .low = 140.31,
        .close = 141.37,
        .volume = 94807600,
    };

    EXPECT_EQ(bar.ts, sys_days{2007y / January / 3});
    EXPECT_DOUBLE_EQ(bar.open, 141.37);
    EXPECT_DOUBLE_EQ(bar.high, 141.84);
    EXPECT_DOUBLE_EQ(bar.low, 140.31);
    EXPECT_DOUBLE_EQ(bar.close, 141.37);
    EXPECT_EQ(bar.volume, 94807600);
}

TEST(Types, TimestampsOrderAndSubtractInWholeDays) {
    const Timestamp earlier{2007y / January / 3};
    const Timestamp later{2007y / January / 5};

    EXPECT_LT(earlier, later);
    EXPECT_EQ((later - earlier).count(), 2);
    EXPECT_EQ(earlier + days{2}, later);
}

TEST(Types, SymbolIsAPlainString) {
    const Symbol symbol = "SPY";
    EXPECT_EQ(symbol, "SPY");
}
