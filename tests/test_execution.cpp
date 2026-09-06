#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include "backtest/execution.hpp"
#include "backtest/portfolio.hpp"

using namespace backtest;
using namespace std::chrono;

namespace {

constexpr Timestamp kDay1{2007y / January / 3};
constexpr Timestamp kDay2{2007y / January / 4};

MarketEvent market(Timestamp ts, const Symbol& symbol, double open, double close) {
    return MarketEvent{ts, symbol,
                       Bar{.ts = ts,
                           .open = open,
                           .high = std::max(open, close),
                           .low = std::min(open, close),
                           .close = close,
                           .volume = 1000}};
}

}  // namespace

TEST(ExecutionHandler, FillsAtTheBarOpenWithAdverseSlippage) {
    ExecutionHandler execution{CostModel{.commission_bps = 0.5, .slippage_bps = 0.5}};

    // A buy fills up from the open.
    execution.submit(OrderEvent{kDay1, "AAA", 100.0});
    std::vector<FillEvent> fills = execution.fill_pending(market(kDay1, "AAA", 50.0, 51.0));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].fill_price, 50.0 * 1.00005);
    EXPECT_DOUBLE_EQ(fills[0].quantity, 100.0);
    EXPECT_DOUBLE_EQ(fills[0].slippage, 100.0 * (50.0 * 1.00005 - 50.0));
    EXPECT_DOUBLE_EQ(fills[0].commission, 5e-5 * 100.0 * 50.0 * 1.00005);

    // A sell fills down from the open.
    execution.submit(OrderEvent{kDay2, "AAA", -100.0});
    fills = execution.fill_pending(market(kDay2, "AAA", 55.0, 56.0));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].fill_price, 55.0 * 0.99995);
    EXPECT_DOUBLE_EQ(fills[0].slippage, 100.0 * (55.0 - 55.0 * 0.99995));
}

TEST(ExecutionHandler, HoldsAnOrderUntilTheNextBarForThatSymbol) {
    ExecutionHandler execution;

    execution.submit(OrderEvent{kDay1, "AAA", 10.0});
    EXPECT_TRUE(execution.has_pending());

    // A bar for a different symbol must not fill it.
    EXPECT_TRUE(execution.fill_pending(market(kDay1, "BBB", 10.0, 10.0)).empty());
    EXPECT_TRUE(execution.has_pending());

    EXPECT_EQ(execution.fill_pending(market(kDay2, "AAA", 20.0, 20.0)).size(), 1u);
    EXPECT_FALSE(execution.has_pending());

    // Once filled it does not fill again.
    EXPECT_TRUE(execution.fill_pending(market(kDay2, "AAA", 20.0, 20.0)).empty());
}

TEST(ExecutionHandler, ANewerOrderSupersedesAnUnfilledOne) {
    ExecutionHandler execution;
    execution.submit(OrderEvent{kDay1, "AAA", 10.0});
    execution.submit(OrderEvent{kDay1, "AAA", 25.0});
    EXPECT_EQ(execution.pending_count(), 1u);

    const std::vector<FillEvent> fills = execution.fill_pending(market(kDay2, "AAA", 10.0, 10.0));
    ASSERT_EQ(fills.size(), 1u);
    // The later target, not the sum of the two.
    EXPECT_DOUBLE_EQ(fills[0].quantity, 25.0);
}

TEST(ExecutionHandler, IgnoresZeroQuantityOrders) {
    ExecutionHandler execution;
    execution.submit(OrderEvent{kDay1, "AAA", 0.0});
    EXPECT_FALSE(execution.has_pending());
}

TEST(ExecutionHandler, ChargesNothingWhenCostsAreDisabled) {
    ExecutionHandler execution{CostModel{.commission_bps = 0.0, .slippage_bps = 0.0}};
    execution.submit(OrderEvent{kDay1, "AAA", 100.0});
    const std::vector<FillEvent> fills = execution.fill_pending(market(kDay1, "AAA", 50.0, 50.0));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].fill_price, 50.0);
    EXPECT_DOUBLE_EQ(fills[0].commission, 0.0);
    EXPECT_DOUBLE_EQ(fills[0].slippage, 0.0);
}

// The acceptance criterion for Step 6: a hand-computed round trip.
//
//   Buy 100 at a 50.00 open, sell 100 at a 55.00 open, 0.5bp commission and
//   0.5bp slippage per side.
//
//   Buy   fill  = 50 * (1 + 5e-5)      = 50.0025
//         notional                     = 5000.25
//         commission = 5e-5 * 5000.25  = 0.2500125
//         slippage   = 100 * 0.0025    = 0.25
//         cash out   = 5000.25 + 0.2500125 = 5000.5000125
//
//   Sell  fill  = 55 * (1 - 5e-5)      = 54.99725
//         notional                     = 5499.725
//         commission = 5e-5 * 5499.725 = 0.27498625
//         slippage   = 100 * 0.00275   = 0.275
//         cash in    = 5499.725 - 0.27498625 = 5499.45001375
//
//   Gross profit  = 500
//   Total costs   = 0.52499875 commission + 0.525 slippage = 1.04999875
//   Net profit    = 500 - 1.04999875 = 498.95000125
TEST(Portfolio, RoundTripCashMatchesTheHandComputedFigureExactly) {
    constexpr double kInitialCapital = 100000.0;
    constexpr double kExpectedNetProfit = 498.95000125;
    constexpr double kExpectedCommission = 0.2500125 + 0.27498625;
    constexpr double kExpectedSlippage = 0.25 + 0.275;

    ExecutionHandler execution{CostModel{.commission_bps = 0.5, .slippage_bps = 0.5}};
    Portfolio portfolio{PortfolioConfig{.initial_capital = kInitialCapital}};

    execution.submit(OrderEvent{kDay1, "AAA", 100.0});
    for (const FillEvent& fill : execution.fill_pending(market(kDay1, "AAA", 50.0, 50.0))) {
        portfolio.on_fill(fill);
    }
    EXPECT_DOUBLE_EQ(portfolio.position("AAA"), 100.0);
    EXPECT_DOUBLE_EQ(portfolio.cash(), kInitialCapital - 5000.5000125);

    execution.submit(OrderEvent{kDay2, "AAA", -100.0});
    for (const FillEvent& fill : execution.fill_pending(market(kDay2, "AAA", 55.0, 55.0))) {
        portfolio.on_fill(fill);
    }

    EXPECT_DOUBLE_EQ(portfolio.position("AAA"), 0.0);
    EXPECT_NEAR(portfolio.cash(), kInitialCapital + kExpectedNetProfit, 1e-9);
    EXPECT_NEAR(portfolio.total_commission(), kExpectedCommission, 1e-12);
    EXPECT_NEAR(portfolio.total_slippage(), kExpectedSlippage, 1e-12);
    EXPECT_NEAR(portfolio.total_costs(), 1.04999875, 1e-12);

    // Flat, so equity is cash, and the gross figure is the net plus the costs
    // that were actually charged: exactly the 500 the price move delivered.
    portfolio.mark_to_market(market(kDay2, "AAA", 55.0, 55.0));
    EXPECT_NEAR(portfolio.equity(), kInitialCapital + kExpectedNetProfit, 1e-9);
    EXPECT_NEAR(portfolio.gross_equity(), kInitialCapital + 500.0, 1e-9);
}

TEST(Portfolio, SizesOrdersFromTargetWeightAndCurrentHolding) {
    Portfolio portfolio{PortfolioConfig{.initial_capital = 100000.0, .min_trade_fraction = 0.0}};
    portfolio.mark_to_market(market(kDay1, "AAA", 50.0, 50.0));

    // Half the book at 50 a share is 1000 shares.
    auto order = portfolio.on_signal(SignalEvent{kDay1, "AAA", 0.5});
    ASSERT_TRUE(order.has_value());
    EXPECT_DOUBLE_EQ(order->quantity, 1000.0);

    // Holding 400 already, the order is the difference, not the target.
    portfolio.on_fill(FillEvent{kDay1, "AAA", 400.0, 50.0, 0.0, 0.0});
    order = portfolio.on_signal(SignalEvent{kDay1, "AAA", 0.5});
    ASSERT_TRUE(order.has_value());
    EXPECT_NEAR(order->quantity, 600.0, 1e-9);

    // A negative weight is a short.
    order = portfolio.on_signal(SignalEvent{kDay1, "AAA", -0.25});
    ASSERT_TRUE(order.has_value());
    EXPECT_NEAR(order->quantity, -900.0, 1e-9);
}

TEST(Portfolio, DeadbandSuppressesTrivialRebalances) {
    Portfolio portfolio{PortfolioConfig{.initial_capital = 100000.0, .min_trade_fraction = 0.01}};
    portfolio.mark_to_market(market(kDay1, "AAA", 50.0, 50.0));
    portfolio.on_fill(FillEvent{kDay1, "AAA", 1000.0, 50.0, 0.0, 0.0});

    // Drift of 0.5% of equity is inside the 1% deadband.
    EXPECT_FALSE(portfolio.on_signal(SignalEvent{kDay1, "AAA", 0.505}).has_value());
    // Drift of 5% is not.
    EXPECT_TRUE(portfolio.on_signal(SignalEvent{kDay1, "AAA", 0.55}).has_value());
}

TEST(Portfolio, WillNotSizeATradeWithoutAPrice) {
    Portfolio portfolio;
    EXPECT_FALSE(portfolio.on_signal(SignalEvent{kDay1, "AAA", 1.0}).has_value());
}
