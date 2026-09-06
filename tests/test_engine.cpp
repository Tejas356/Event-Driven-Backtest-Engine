#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <vector>

#include "backtest/engine.hpp"

using namespace backtest;
using namespace std::chrono;

namespace {

Bar bar_on(int y, unsigned m, unsigned d, double open, double close) {
    return Bar{.ts = Timestamp{year{y} / month{m} / day{d}},
               .open = open,
               .high = std::max(open, close) + 1.0,
               .low = std::min(open, close) - 1.0,
               .close = close,
               .volume = 1000};
}

// Five consecutive days with distinct open and close prices, so a fill at the
// wrong one is visible in the resulting cash.
std::map<Symbol, std::vector<Bar>> price_series() {
    return {{"AAA",
             {bar_on(2007, 1, 2, 100.0, 101.0), bar_on(2007, 1, 3, 102.0, 103.0),
              bar_on(2007, 1, 4, 104.0, 105.0), bar_on(2007, 1, 5, 106.0, 107.0),
              bar_on(2007, 1, 8, 108.0, 109.0)}}};
}

// Emits nothing, ever.
class NullStrategy : public Strategy {
public:
    std::vector<SignalEvent> on_market(const MarketEvent&, const DataHandler&) override {
        return {};
    }
    const char* name() const noexcept override { return "null"; }
};

// Emits a single target weight on the first bar it sees and never again.
class OneShotStrategy : public Strategy {
public:
    explicit OneShotStrategy(double weight) : weight_(weight) {}

    std::vector<SignalEvent> on_market(const MarketEvent& event, const DataHandler&) override {
        if (fired_) {
            return {};
        }
        fired_ = true;
        signal_time_ = event.ts;
        return {SignalEvent{event.ts, event.symbol, weight_}};
    }
    const char* name() const noexcept override { return "one_shot"; }

    Timestamp signal_time() const { return signal_time_; }

private:
    double weight_;
    bool fired_ = false;
    Timestamp signal_time_{};
};

}  // namespace

TEST(Engine, EquityCurveHasOneRowPerTimestamp) {
    DataHandler data = DataHandler::from_bars(price_series());
    NullStrategy strategy;
    Portfolio portfolio;
    ExecutionHandler execution;

    const std::size_t timestamps = data.timeline_size();
    const RunStatistics stats = Engine(data, strategy, portfolio, execution).run();

    EXPECT_EQ(stats.timestamps, timestamps);
    EXPECT_EQ(portfolio.equity_curve().size(), timestamps);
    EXPECT_EQ(stats.market_events, 5u);
}

TEST(Engine, ZeroSignalStrategyLeavesEquityExactlyFlat) {
    DataHandler data = DataHandler::from_bars(price_series());
    NullStrategy strategy;
    Portfolio portfolio{PortfolioConfig{.initial_capital = 1000000.0}};
    ExecutionHandler execution;

    const RunStatistics stats = Engine(data, strategy, portfolio, execution).run();

    EXPECT_EQ(stats.signals, 0u);
    EXPECT_EQ(stats.orders, 0u);
    EXPECT_EQ(stats.fills, 0u);
    for (const EquityPoint& point : portfolio.equity_curve()) {
        // Exactly, not approximately: an idle portfolio that drifts is an
        // accounting bug.
        EXPECT_DOUBLE_EQ(point.equity, 1000000.0);
        EXPECT_DOUBLE_EQ(point.gross_exposure, 0.0);
    }
    EXPECT_DOUBLE_EQ(portfolio.total_costs(), 0.0);
}

// The second structural claim of the engine, after look-ahead: a signal
// computed from bar t close cannot be filled at any price bar t offered.
//
// The fixture makes the three candidate prices distinct. The signal fires on
// 2 Jan, whose open is 100 and close is 101; the correct fill is 3 Jan open,
// 102. Filling at the signal bar close (101) or its open (100) -- the two
// classic ways to manufacture profit -- both fail here.
TEST(Engine, ASignalIsNeverFilledAtAPriceFromTheBarThatGeneratedIt) {
    DataHandler data = DataHandler::from_bars(price_series());
    OneShotStrategy strategy{1.0};
    Portfolio portfolio{PortfolioConfig{.initial_capital = 100000.0}};
    ExecutionHandler execution{CostModel{.commission_bps = 0.5, .slippage_bps = 0.5}};

    const RunStatistics stats = Engine(data, strategy, portfolio, execution).run();

    EXPECT_EQ(stats.signals, 1u);
    EXPECT_EQ(stats.orders, 1u);
    ASSERT_EQ(stats.fills, 1u);

    const FillEvent& fill = portfolio.fills().front();
    EXPECT_EQ(strategy.signal_time(), Timestamp{2007y / January / 2});
    EXPECT_EQ(fill.ts, Timestamp{2007y / January / 3});
    EXPECT_GT(fill.ts, strategy.signal_time()) << "fill must be strictly after the signal";

    // 3 Jan open, marked up by half a basis point of slippage.
    EXPECT_DOUBLE_EQ(fill.fill_price, 102.0 * 1.00005);
    EXPECT_NE(fill.fill_price, 101.0);  // the close that generated the signal
    EXPECT_NE(fill.fill_price, 100.0);  // the open of that same bar

    // Sized from the 2 Jan close of 101, since that was the latest price
    // known when the order was raised.
    EXPECT_NEAR(fill.quantity, 100000.0 / 101.0, 1e-9);
}

TEST(Engine, CostsAreChargedOnEveryUnitOfTurnover) {
    DataHandler data = DataHandler::from_bars(price_series());
    OneShotStrategy strategy{1.0};
    Portfolio portfolio{PortfolioConfig{.initial_capital = 100000.0}};
    ExecutionHandler execution{CostModel{.commission_bps = 0.5, .slippage_bps = 0.5}};

    Engine(data, strategy, portfolio, execution).run();

    ASSERT_EQ(portfolio.fills().size(), 1u);
    const FillEvent& fill = portfolio.fills().front();

    EXPECT_GT(portfolio.total_costs(), 0.0);
    EXPECT_DOUBLE_EQ(portfolio.total_commission(), fill.commission);
    EXPECT_DOUBLE_EQ(portfolio.total_slippage(), fill.slippage);
    EXPECT_NEAR(portfolio.total_traded_notional(), std::abs(fill.quantity) * fill.fill_price,
                1e-9);

    // The gross curve sits above the net curve by exactly the costs paid.
    const EquityPoint& last = portfolio.equity_curve().back();
    EXPECT_NEAR(last.gross_equity - last.equity, portfolio.total_costs(), 1e-9);
    EXPECT_GT(last.gross_equity, last.equity);
}

TEST(Engine, WithoutCostsAFullyInvestedBookTracksThePriceExactly) {
    // Buying at the 3 Jan open of 102 and holding to the 8 Jan close of 109
    // must return exactly 109/102 - 1 on the invested capital.
    DataHandler data = DataHandler::from_bars(price_series());
    OneShotStrategy strategy{1.0};
    Portfolio portfolio{PortfolioConfig{.initial_capital = 100000.0}};
    ExecutionHandler execution{CostModel{.commission_bps = 0.0, .slippage_bps = 0.0}};

    Engine(data, strategy, portfolio, execution).run();

    const double quantity = 100000.0 / 101.0;
    const double expected = 100000.0 - quantity * 102.0 + quantity * 109.0;
    EXPECT_NEAR(portfolio.equity_curve().back().equity, expected, 1e-6);
    EXPECT_NEAR(portfolio.equity_curve().back().gross_exposure,
                quantity * 109.0 / expected, 1e-9);
}

TEST(Engine, MarksToMarketBeforeTheFirstFillSoEarlyEquityIsUnchanged) {
    DataHandler data = DataHandler::from_bars(price_series());
    OneShotStrategy strategy{1.0};
    Portfolio portfolio{PortfolioConfig{.initial_capital = 100000.0}};
    ExecutionHandler execution;

    Engine(data, strategy, portfolio, execution).run();

    // Nothing is held on the first bar, because the signal raised there
    // cannot fill until the second.
    EXPECT_DOUBLE_EQ(portfolio.equity_curve().front().equity, 100000.0);
    EXPECT_DOUBLE_EQ(portfolio.equity_curve().front().gross_exposure, 0.0);
    EXPECT_GT(portfolio.equity_curve()[1].gross_exposure, 0.0);
}
