// The central correctness claim of this engine.
//
// The argument for an event-driven design over a vectorised one is that
// look-ahead bias becomes structurally impossible rather than merely avoided
// by discipline. That claim is only worth making if it is checked, so this
// file iterates an entire dataset and asserts, on every call, at every
// simulation step, for every symbol and every lookback length, that no bar
// handed back carries a timestamp later than the current simulation time.
//
// The reference data here is constructed independently. It is deliberately
// not read back out of the DataHandler, because the DataHandler has no method
// that would return it -- which is the property under test.

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <random>
#include <span>
#include <vector>

#include "backtest/data_handler.hpp"

using namespace backtest;
using namespace std::chrono;

namespace {

// Roughly ten years of business days across four symbols with staggered
// listing dates and scattered missing days, so the walk covers absent
// history, forward-filled gaps and ordinary bars.
std::map<Symbol, std::vector<Bar>> build_dataset() {
    std::mt19937 rng(20070103);
    std::uniform_real_distribution<double> shock(-0.02, 0.02);
    std::uniform_int_distribution<int> skip(0, 40);

    const std::vector<Symbol> symbols{"AAA", "BBB", "CCC", "DDD"};
    std::map<Symbol, std::vector<Bar>> dataset;

    for (std::size_t s = 0; s < symbols.size(); ++s) {
        // Staggered starts: each symbol lists a quarter later than the last.
        const int start_offset = static_cast<int>(s) * 90;
        double price = 50.0 + 10.0 * static_cast<double>(s);
        std::vector<Bar> bars;

        for (int d = start_offset; d < 3650; ++d) {  // about ten years
            const Timestamp ts = Timestamp{2007y / January / 3} + days{d};
            const weekday wd{ts};
            if (wd == Saturday || wd == Sunday) {
                continue;
            }
            // Drop the occasional day so some symbols are missing bars that
            // others have, which is what forces the forward-fill path.
            if (skip(rng) == 0) {
                continue;
            }
            price *= (1.0 + shock(rng));
            bars.push_back(Bar{.ts = ts,
                               .open = price * 0.999,
                               .high = price * 1.005,
                               .low = price * 0.995,
                               .close = price,
                               .volume = 100000});
        }
        dataset.emplace(symbols[s], std::move(bars));
    }
    return dataset;
}

}  // namespace

TEST(LookAhead, NoBarEverReturnedIsNewerThanTheCurrentSimulationTime) {
    const std::map<Symbol, std::vector<Bar>> dataset = build_dataset();
    DataHandler data = DataHandler::from_bars(dataset);

    // A range of lookback lengths, including ones far longer than the
    // available history and the 252-day window the momentum signal will use.
    const std::vector<std::size_t> lookbacks{1, 2, 5, 21, 63, 252, 1000, 100000};

    std::size_t steps = 0;
    std::size_t bars_checked = 0;

    while (data.has_more_bars()) {
        data.advance();
        const Timestamp now = data.current_time();
        ++steps;

        for (const Symbol& symbol : data.symbols()) {
            for (std::size_t n : lookbacks) {
                const std::span<const Bar> bars = data.latest_bars(symbol, n);

                ASSERT_LE(bars.size(), n) << "returned more bars than requested";

                for (std::size_t i = 0; i < bars.size(); ++i) {
                    // The claim, asserted on every single bar handed out.
                    ASSERT_LE(bars[i].ts, now)
                        << "look-ahead: " << symbol << " bar at index " << i
                        << " is dated after the current simulation time";
                    if (i > 0) {
                        ASSERT_LT(bars[i - 1].ts, bars[i].ts)
                            << "lookback window is not in ascending date order";
                    }
                    ++bars_checked;
                }

                // The newest bar must be the current time exactly, once the
                // symbol has listed: a lag here would be just as wrong as a
                // lead, silently feeding the strategy stale prices.
                if (!bars.empty()) {
                    ASSERT_EQ(bars.back().ts, now);
                }
            }
        }
    }

    EXPECT_EQ(steps, data.timeline_size());
    EXPECT_GT(steps, 2400u) << "the dataset should be substantial enough to be meaningful";
    EXPECT_GT(bars_checked, 1000000u);
}

TEST(LookAhead, TheNewestBarMatchesTheIndependentlyKnownPriceForThatDate) {
    // Beyond ordering, the bar handed to a strategy must be the right bar.
    // The expected close is looked up in the independently held dataset, so a
    // shifted or misaligned series fails here rather than passing quietly.
    const std::map<Symbol, std::vector<Bar>> dataset = build_dataset();
    DataHandler data = DataHandler::from_bars(dataset);

    std::map<Symbol, std::size_t> next_index;
    for (const auto& [symbol, bars] : dataset) {
        next_index[symbol] = 0;
    }

    while (data.has_more_bars()) {
        const std::vector<MarketEvent> events = data.advance();
        const Timestamp now = data.current_time();

        for (const MarketEvent& event : events) {
            const std::vector<Bar>& reference = dataset.at(event.symbol);
            std::size_t& index = next_index[event.symbol];

            ASSERT_LT(index, reference.size());
            // Market events must arrive in the same order, and on the same
            // dates, as the source data.
            EXPECT_EQ(reference[index].ts, now);
            EXPECT_DOUBLE_EQ(event.bar.close, reference[index].close);
            EXPECT_EQ(event.bar.ts, now);

            const std::span<const Bar> latest = data.latest_bars(event.symbol, 1);
            ASSERT_EQ(latest.size(), 1u);
            EXPECT_DOUBLE_EQ(latest[0].close, reference[index].close);
            ++index;
        }
    }

    // Every bar in the source data was delivered exactly once.
    for (const auto& [symbol, bars] : dataset) {
        EXPECT_EQ(next_index[symbol], bars.size()) << "undelivered bars for " << symbol;
    }
}
