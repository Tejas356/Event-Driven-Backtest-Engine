#pragma once

#include <chrono>
#include <string>

// Core value types. Types only -- no logic lives in this header.
namespace backtest {

// Daily bars, so a date is a sufficient timestamp. Choosing sys_days over a
// full time_point keeps comparison and arithmetic in whole days and removes
// any question of timezone handling.
using Timestamp = std::chrono::sys_days;

// A SymbolId interning scheme would cut the string compares in the hot loop,
// but that is an optimisation to make once there is something to profile.
using Symbol = std::string;

using Price = double;

// Fractional shares are permitted: this is a research engine sizing by weight,
// not an order router that has to respect lot sizes.
using Quantity = double;

struct Bar {
    Timestamp ts;
    Price open;
    Price high;
    Price low;
    Price close;
    long volume;
};

}  // namespace backtest
