#include <iostream>

#include "backtest/version.hpp"

// Placeholder runner. Step 12 turns this into the real backtest driver:
// config parsing, engine run, metrics table, CSV/JSON output under results/.
int main() {
    std::cout << "tsmom-engine " << backtest::version() << '\n';
    return 0;
}
