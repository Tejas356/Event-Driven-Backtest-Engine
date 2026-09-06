#include "backtest/engine.hpp"

#include <optional>
#include <vector>

namespace backtest {

RunStatistics Engine::run() {
    RunStatistics stats;

    while (data_.has_more_bars()) {
        for (const MarketEvent& event : data_.advance()) {
            queue_.push(event);
        }

        // Drain everything that happens at this timestamp before time moves.
        // Two passes: the first processes market events and the fills they
        // release, the second the signals raised once the whole universe is
        // marked to market at today close.
        bool bar_closed = false;
        while (true) {
        while (auto event = queue_.pop()) {
            std::visit(
                Overloaded{
                    [&](const MarketEvent& market) {
                        ++stats.market_events;

                        // Orders resting from a previous bar fill here, at
                        // this bar open. This ordering is the whole of the
                        // signal lag: an order raised below, from this bar
                        // close, cannot reach a fill until the next bar
                        // arrives, because the execution handler is only ever
                        // given the chance to fill on a market event.
                        for (const FillEvent& fill : execution_.fill_pending(market)) {
                            queue_.push(fill);
                        }

                        portfolio_.mark_to_market(market);

                        for (const SignalEvent& signal : strategy_.on_market(market, data_)) {
                            ++stats.signals;
                            queue_.push(signal);
                        }
                    },
                    [&](const SignalEvent& signal) {
                        if (auto order = portfolio_.on_signal(signal)) {
                            ++stats.orders;
                            queue_.push(*order);
                        }
                    },
                    [&](const OrderEvent& order) {
                        // Submitted, deliberately not filled. It rests until
                        // the next market event for this symbol.
                        execution_.submit(order);
                    },
                    [&](const FillEvent& fill) {
                        ++stats.fills;
                        portfolio_.on_fill(fill);
                    },
                },
                *event);
        }

            if (bar_closed) {
                break;
            }
            bar_closed = true;
            for (const SignalEvent& signal : strategy_.on_bar_close(data_.current_time(), data_)) {
                ++stats.signals;
                queue_.push(signal);
            }
        }

        portfolio_.record_equity(data_.current_time());
        ++stats.timestamps;
    }

    return stats;
}

}  // namespace backtest
