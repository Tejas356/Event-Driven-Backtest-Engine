#pragma once

#include <cstddef>

#include "backtest/data_handler.hpp"
#include "backtest/event_queue.hpp"
#include "backtest/execution.hpp"
#include "backtest/portfolio.hpp"
#include "backtest/strategy.hpp"

namespace backtest {

// Counters for a completed run, useful for asserting on behaviour that the
// equity curve alone would not reveal.
struct RunStatistics {
    std::size_t timestamps = 0;
    std::size_t market_events = 0;
    std::size_t signals = 0;
    std::size_t orders = 0;
    std::size_t fills = 0;
};

// Wires the components together and runs the event loop.
//
// The outer loop advances simulation time; the inner loop drains every event
// occurring at that time before time moves on. That is what keeps causality
// straight: nothing generated at bar t can act on a price from bar t+1,
// because bar t+1 does not exist yet when the inner loop runs.
class Engine {
public:
    Engine(DataHandler& data, Strategy& strategy, Portfolio& portfolio,
           ExecutionHandler& execution)
        : data_(data), strategy_(strategy), portfolio_(portfolio), execution_(execution) {}

    RunStatistics run();

private:
    DataHandler& data_;
    Strategy& strategy_;
    Portfolio& portfolio_;
    ExecutionHandler& execution_;
    EventQueue queue_;
};

}  // namespace backtest
