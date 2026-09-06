#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "backtest/event.hpp"
#include "backtest/event_queue.hpp"

using namespace backtest;
using namespace std::chrono;

namespace {

constexpr Timestamp kDay{2007y / January / 3};

Bar make_bar(double close) {
    return Bar{.ts = kDay, .open = close, .high = close, .low = close, .close = close, .volume = 1};
}

}  // namespace

TEST(EventQueue, PopsInFifoOrder) {
    EventQueue queue;
    queue.push(MarketEvent{kDay, "SPY", make_bar(100.0)});
    queue.push(SignalEvent{kDay, "SPY", 1.0});
    queue.push(OrderEvent{kDay, "SPY", 10.0});
    queue.push(FillEvent{kDay, "SPY", 10.0, 100.0, 0.05, 0.05});

    EXPECT_EQ(queue.size(), 4u);

    std::vector<std::size_t> order;
    while (auto event = queue.pop()) {
        order.push_back(event->index());
    }

    EXPECT_EQ(order, (std::vector<std::size_t>{0, 1, 2, 3}));
    EXPECT_TRUE(queue.empty());
}

TEST(EventQueue, PopOnEmptyReturnsNullopt) {
    EventQueue queue;
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.pop().has_value());

    queue.push(OrderEvent{kDay, "IEF", -5.0});
    EXPECT_TRUE(queue.pop().has_value());
    EXPECT_FALSE(queue.pop().has_value());
}

TEST(EventQueue, InterleavedPushAndPopStaysFifo) {
    EventQueue queue;
    queue.push(OrderEvent{kDay, "A", 1.0});
    queue.push(OrderEvent{kDay, "B", 2.0});

    auto first = queue.pop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(std::get<OrderEvent>(*first).symbol, "A");

    queue.push(OrderEvent{kDay, "C", 3.0});

    std::vector<Symbol> remaining;
    while (auto event = queue.pop()) {
        remaining.push_back(std::get<OrderEvent>(*event).symbol);
    }
    EXPECT_EQ(remaining, (std::vector<Symbol>{"B", "C"}));
}

TEST(EventQueue, ClearDiscardsEverything) {
    EventQueue queue;
    queue.push(SignalEvent{kDay, "GLD", 0.5});
    queue.push(SignalEvent{kDay, "TLT", -0.5});
    queue.clear();
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0u);
}

TEST(Event, VisitDispatchesEachAlternativeToItsOwnHandler) {
    const std::vector<Event> events{
        MarketEvent{kDay, "SPY", make_bar(100.0)},
        SignalEvent{kDay, "SPY", 0.25},
        OrderEvent{kDay, "SPY", -3.0},
        FillEvent{kDay, "SPY", -3.0, 99.5, 0.01, 0.02},
    };

    std::vector<std::string> seen;
    for (const Event& event : events) {
        std::visit(Overloaded{
                       [&](const MarketEvent& e) { seen.push_back("market:" + e.symbol); },
                       [&](const SignalEvent& e) {
                           seen.push_back("signal:" + std::to_string(e.target_weight));
                       },
                       [&](const OrderEvent& e) {
                           seen.push_back("order:" + std::to_string(e.quantity));
                       },
                       [&](const FillEvent& e) {
                           seen.push_back("fill:" + std::to_string(e.fill_price));
                       },
                   },
                   event);
    }

    ASSERT_EQ(seen.size(), 4u);
    EXPECT_EQ(seen[0], "market:SPY");
    EXPECT_EQ(seen[1], "signal:" + std::to_string(0.25));
    EXPECT_EQ(seen[2], "order:" + std::to_string(-3.0));
    EXPECT_EQ(seen[3], "fill:" + std::to_string(99.5));
}

TEST(Event, EventTimeReadsTheTimestampOfAnyAlternative) {
    const Timestamp later{2008y / March / 14};
    EXPECT_EQ(event_time(Event{MarketEvent{kDay, "SPY", make_bar(1.0)}}), kDay);
    EXPECT_EQ(event_time(Event{SignalEvent{later, "SPY", 1.0}}), later);
    EXPECT_EQ(event_time(Event{OrderEvent{later, "SPY", 1.0}}), later);
    EXPECT_EQ(event_time(Event{FillEvent{kDay, "SPY", 1.0, 1.0, 0.0, 0.0}}), kDay);
}
