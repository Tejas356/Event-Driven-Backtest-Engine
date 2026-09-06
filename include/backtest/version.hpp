#pragma once

namespace backtest {

// Version of the engine, so a results file can record which build produced it.
const char* version() noexcept;

}  // namespace backtest
