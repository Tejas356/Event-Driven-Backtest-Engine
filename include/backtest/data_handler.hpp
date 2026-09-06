#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "backtest/event.hpp"
#include "backtest/types.hpp"

namespace backtest {

// Streams bars in timestamp order and exposes a bounded lookback.
//
// This class is where the central correctness claim of the engine lives. A
// strategy cannot see the future because there is no method here that would
// show it the future: the only read API is latest_bars(), which is clamped to
// a private cursor. There is deliberately no accessor returning a whole
// series -- not for convenience, and not for tests. A test that needs the
// full data constructs it independently.
//
// Alignment: the handler builds the union of every symbol timeline and holds
// each symbol on that shared grid, forward-filling gaps so that
// latest_bars(sym, 252) always spans the same 252 dates for every symbol.
// Before a symbol has any history it is simply absent rather than
// back-filled, so a late-listing ETF cannot contribute a fabricated price.
class DataHandler {
public:
    // Loads <directory>/<symbol>.csv for each symbol, expecting the header
    // date,open,high,low,close,volume with rows in ascending date order.
    static DataHandler from_csv_directory(const std::filesystem::path& directory,
                                          const std::vector<Symbol>& symbols);

    // In-memory construction, for tests and for generated data. Bars per
    // symbol must be in ascending date order with no duplicates.
    static DataHandler from_bars(std::map<Symbol, std::vector<Bar>> bars_by_symbol);

    // True while advance() still has a timestamp to move to.
    bool has_more_bars() const noexcept;

    // Moves to the next timestamp and returns the market events occurring at
    // it -- one per symbol that genuinely traded. Forward-filled placeholders
    // are not emitted: the engine never pretends a bar exists on a date a
    // symbol did not trade, though the filled value remains visible through
    // latest_bars so lookback windows stay date-aligned.
    std::vector<MarketEvent> advance();

    // At most n bars ending at the current simulation time, oldest first.
    // Returns fewer than n when history is short and an empty span when the
    // symbol has no history yet; the caller checks size() before acting.
    std::span<const Bar> latest_bars(const Symbol& symbol, std::size_t n) const;

    // Throws if called before the first advance().
    Timestamp current_time() const;

    const std::vector<Symbol>& symbols() const noexcept { return symbols_; }

    // Number of timestamps on the union timeline. This is a property of the
    // schedule, not of any price, so it reveals nothing about future prices.
    std::size_t timeline_size() const noexcept { return timeline_.size(); }

private:
    struct SymbolSeries {
        // Aligned to timeline_[start_index + i].
        std::vector<Bar> bars;
        // Index into timeline_ of this symbol first real bar.
        std::size_t start_index = 0;
        // False where the bar at that position was forward-filled.
        std::vector<bool> traded;
    };

    DataHandler(std::vector<Timestamp> timeline, std::vector<Symbol> symbols,
                std::map<Symbol, SymbolSeries> series);

    static constexpr std::size_t kBeforeStart = static_cast<std::size_t>(-1);

    std::vector<Timestamp> timeline_;
    std::vector<Symbol> symbols_;
    std::map<Symbol, SymbolSeries> series_;
    // Nothing at an index greater than cursor_ is reachable through any
    // public method on this class.
    std::size_t cursor_ = kBeforeStart;
};

// Parses one CSV in the fetch_data.py format. Exposed so that data problems
// surface as a loader error with a line number rather than as a silently
// wrong backtest.
std::vector<Bar> read_bars_csv(const std::filesystem::path& file);

}  // namespace backtest
