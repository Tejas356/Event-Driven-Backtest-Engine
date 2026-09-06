#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <utility>

#include "backtest/event.hpp"

namespace backtest {

// FIFO over a deque. Within a single timestamp FIFO *is* the causal order --
// a market event precedes the signal it triggers, which precedes the order,
// which precedes the fill -- so a priority queue would buy nothing. Revisit
// this if intraday data ever puts several timestamps in flight at once.
class EventQueue {
public:
    void push(Event event) { events_.push_back(std::move(event)); }

    // Returns nullopt when empty rather than requiring a separate empty()
    // check, so the drain loop cannot pop past the end.
    std::optional<Event> pop() {
        if (events_.empty()) {
            return std::nullopt;
        }
        Event front = std::move(events_.front());
        events_.pop_front();
        return front;
    }

    bool empty() const noexcept { return events_.empty(); }
    std::size_t size() const noexcept { return events_.size(); }
    void clear() noexcept { events_.clear(); }

private:
    std::deque<Event> events_;
};

}  // namespace backtest
