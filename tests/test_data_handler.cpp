#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "backtest/data_handler.hpp"

using namespace backtest;
using namespace std::chrono;
namespace fs = std::filesystem;

namespace {

Bar bar_on(int y, unsigned m, unsigned d, double close) {
    return Bar{.ts = Timestamp{year{y} / month{m} / day{d}},
               .open = close - 0.5,
               .high = close + 1.0,
               .low = close - 1.0,
               .close = close,
               .volume = 1000};
}

// A two-symbol set with the awkward cases built in: BBB starts a day later
// than AAA and does not trade on the 4th, so the fixture exercises both the
// absent-history path and the forward-fill path.
std::map<Symbol, std::vector<Bar>> sample_bars() {
    return {
        {"AAA",
         {bar_on(2007, 1, 2, 100.0), bar_on(2007, 1, 3, 101.0), bar_on(2007, 1, 4, 102.0),
          bar_on(2007, 1, 5, 103.0), bar_on(2007, 1, 8, 104.0)}},
        {"BBB", {bar_on(2007, 1, 3, 50.0), bar_on(2007, 1, 5, 52.0), bar_on(2007, 1, 8, 55.0)}},
    };
}

// A scratch directory that cleans up after itself, for the CSV loader tests.
class TempDir {
public:
    TempDir() : path_(fs::temp_directory_path() / fs::path("tsmom_test_" + std::to_string(
                                                      std::chrono::steady_clock::now()
                                                          .time_since_epoch()
                                                          .count()))) {
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

    void write(const std::string& name, const std::string& contents) const {
        std::ofstream out(path_ / name);
        out << contents;
    }

private:
    fs::path path_;
};

}  // namespace

TEST(DataHandler, StreamsTheUnionOfAllSymbolTimestamps) {
    DataHandler data = DataHandler::from_bars(sample_bars());

    // AAA has 5 dates, BBB has 3, all of them within AAA set: union is 5.
    EXPECT_EQ(data.timeline_size(), 5u);
    EXPECT_EQ(data.symbols(), (std::vector<Symbol>{"AAA", "BBB"}));

    std::vector<Timestamp> visited;
    while (data.has_more_bars()) {
        data.advance();
        visited.push_back(data.current_time());
    }

    const std::vector<Timestamp> expected{
        Timestamp{2007y / January / 2}, Timestamp{2007y / January / 3},
        Timestamp{2007y / January / 4}, Timestamp{2007y / January / 5},
        Timestamp{2007y / January / 8}};
    EXPECT_EQ(visited, expected);
    EXPECT_FALSE(data.has_more_bars());
    EXPECT_THROW(data.advance(), std::logic_error);
}

TEST(DataHandler, EmitsMarketEventsOnlyForSymbolsThatActuallyTraded) {
    DataHandler data = DataHandler::from_bars(sample_bars());

    // 2 Jan: BBB has no history yet.
    auto events = data.advance();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].symbol, "AAA");

    // 3 Jan: both trade.
    events = data.advance();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].symbol, "AAA");
    EXPECT_EQ(events[1].symbol, "BBB");

    // 4 Jan: BBB did not trade, so no event is fabricated for it.
    events = data.advance();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].symbol, "AAA");
    EXPECT_EQ(events[0].bar.ts, data.current_time());
}

TEST(DataHandler, LatestBarsReturnsWhatHistoryExistsAndNoMore) {
    DataHandler data = DataHandler::from_bars(sample_bars());

    // Advance to cursor 2, the third timestamp.
    data.advance();
    data.advance();
    data.advance();
    ASSERT_EQ(data.current_time(), Timestamp{2007y / January / 4});

    // Asking for 5 with 3 bars of history returns 3, oldest first.
    const std::span<const Bar> bars = data.latest_bars("AAA", 5);
    ASSERT_EQ(bars.size(), 3u);
    EXPECT_DOUBLE_EQ(bars[0].close, 100.0);
    EXPECT_DOUBLE_EQ(bars[1].close, 101.0);
    EXPECT_DOUBLE_EQ(bars[2].close, 102.0);
    for (const Bar& bar : bars) {
        EXPECT_LE(bar.ts, data.current_time());
    }

    // A shorter request is truncated from the front, keeping the newest.
    const std::span<const Bar> two = data.latest_bars("AAA", 2);
    ASSERT_EQ(two.size(), 2u);
    EXPECT_DOUBLE_EQ(two[0].close, 101.0);
    EXPECT_DOUBLE_EQ(two[1].close, 102.0);
}

TEST(DataHandler, ReturnsNothingBeforeTheFirstAdvanceOrForUnknownSymbols) {
    DataHandler data = DataHandler::from_bars(sample_bars());

    EXPECT_TRUE(data.latest_bars("AAA", 5).empty());
    EXPECT_THROW(data.current_time(), std::logic_error);

    data.advance();
    EXPECT_TRUE(data.latest_bars("ZZZ", 5).empty());
    EXPECT_TRUE(data.latest_bars("AAA", 0).empty());
}

TEST(DataHandler, SymbolWithNoHistoryYetIsAbsentRatherThanBackFilled) {
    DataHandler data = DataHandler::from_bars(sample_bars());

    data.advance();  // 2 Jan, before BBB lists
    EXPECT_TRUE(data.latest_bars("BBB", 10).empty());

    data.advance();  // 3 Jan, BBB first bar
    const std::span<const Bar> bars = data.latest_bars("BBB", 10);
    ASSERT_EQ(bars.size(), 1u);
    EXPECT_DOUBLE_EQ(bars[0].close, 50.0);
    EXPECT_EQ(bars[0].ts, Timestamp{2007y / January / 3});
}

TEST(DataHandler, ForwardFillsGapsWithoutMisalignment) {
    DataHandler data = DataHandler::from_bars(sample_bars());

    data.advance();  // 2 Jan
    data.advance();  // 3 Jan
    data.advance();  // 4 Jan -- BBB does not trade

    const std::span<const Bar> bars = data.latest_bars("BBB", 10);
    ASSERT_EQ(bars.size(), 2u);

    // The filled bar carries the current date, not the stale one, so it can
    // never claim to be newer than it is; the price is the last known close.
    EXPECT_EQ(bars[1].ts, Timestamp{2007y / January / 4});
    EXPECT_DOUBLE_EQ(bars[1].close, 50.0);
    EXPECT_DOUBLE_EQ(bars[1].open, 50.0);
    EXPECT_EQ(bars[1].volume, 0);

    // Alignment is the point: after the gap both symbols report the same
    // number of bars over the same dates.
    data.advance();  // 5 Jan
    EXPECT_EQ(data.latest_bars("AAA", 3).size(), 3u);
    EXPECT_EQ(data.latest_bars("BBB", 3).size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(data.latest_bars("AAA", 3)[i].ts, data.latest_bars("BBB", 3)[i].ts);
    }
}

TEST(DataHandler, RejectsOutOfOrderOrEmptyInput) {
    std::map<Symbol, std::vector<Bar>> descending{
        {"AAA", {bar_on(2007, 1, 3, 101.0), bar_on(2007, 1, 2, 100.0)}}};
    EXPECT_THROW(DataHandler::from_bars(descending), std::invalid_argument);

    std::map<Symbol, std::vector<Bar>> duplicated{
        {"AAA", {bar_on(2007, 1, 2, 100.0), bar_on(2007, 1, 2, 101.0)}}};
    EXPECT_THROW(DataHandler::from_bars(duplicated), std::invalid_argument);

    EXPECT_THROW(DataHandler::from_bars({}), std::invalid_argument);
    EXPECT_THROW(DataHandler::from_bars({{"AAA", {}}}), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// CSV loading
// ---------------------------------------------------------------------------

TEST(ReadBarsCsv, ParsesTheFetchScriptFormat) {
    TempDir dir;
    dir.write("AAA.csv",
              "date,open,high,low,close,volume\n"
              "2007-01-02,99.5,101.0,99.0,100.0,1000\n"
              "2007-01-03,100.5,102.0,100.0,101.0,2000\n");

    const std::vector<Bar> bars = read_bars_csv(dir.path() / "AAA.csv");
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_EQ(bars[0].ts, Timestamp{2007y / January / 2});
    EXPECT_DOUBLE_EQ(bars[0].open, 99.5);
    EXPECT_DOUBLE_EQ(bars[0].close, 100.0);
    EXPECT_EQ(bars[0].volume, 1000);
    EXPECT_EQ(bars[1].ts, Timestamp{2007y / January / 3});
}

TEST(ReadBarsCsv, ToleratesCrlfLineEndingsAndAByteOrderMark) {
    TempDir dir;
    dir.write("AAA.csv",
              "\xEF\xBB\xBF"
              "date,open,high,low,close,volume\r\n"
              "2007-01-02,99.5,101.0,99.0,100.0,1000\r\n");

    const std::vector<Bar> bars = read_bars_csv(dir.path() / "AAA.csv");
    ASSERT_EQ(bars.size(), 1u);
    EXPECT_DOUBLE_EQ(bars[0].close, 100.0);
}

TEST(ReadBarsCsv, RejectsBadDataLoudly) {
    TempDir dir;

    dir.write("bad_date.csv",
              "date,open,high,low,close,volume\n"
              "02/01/2007,99.5,101.0,99.0,100.0,1000\n");
    EXPECT_THROW(read_bars_csv(dir.path() / "bad_date.csv"), std::runtime_error);

    dir.write("impossible_date.csv",
              "date,open,high,low,close,volume\n"
              "2007-02-30,99.5,101.0,99.0,100.0,1000\n");
    EXPECT_THROW(read_bars_csv(dir.path() / "impossible_date.csv"), std::runtime_error);

    dir.write("short_row.csv",
              "date,open,high,low,close,volume\n"
              "2007-01-02,99.5,101.0\n");
    EXPECT_THROW(read_bars_csv(dir.path() / "short_row.csv"), std::runtime_error);

    dir.write("negative_price.csv",
              "date,open,high,low,close,volume\n"
              "2007-01-02,99.5,101.0,99.0,-100.0,1000\n");
    EXPECT_THROW(read_bars_csv(dir.path() / "negative_price.csv"), std::runtime_error);

    dir.write("unsorted.csv",
              "date,open,high,low,close,volume\n"
              "2007-01-03,100.5,102.0,100.0,101.0,2000\n"
              "2007-01-02,99.5,101.0,99.0,100.0,1000\n");
    EXPECT_THROW(read_bars_csv(dir.path() / "unsorted.csv"), std::runtime_error);

    dir.write("empty.csv", "date,open,high,low,close,volume\n");
    EXPECT_THROW(read_bars_csv(dir.path() / "empty.csv"), std::runtime_error);

    EXPECT_THROW(read_bars_csv(dir.path() / "missing.csv"), std::runtime_error);
}

TEST(DataHandler, LoadsADirectoryOfCsvsAndStreamsThem) {
    TempDir dir;
    dir.write("AAA.csv",
              "date,open,high,low,close,volume\n"
              "2007-01-02,99.5,101.0,99.0,100.0,1000\n"
              "2007-01-03,100.5,102.0,100.0,101.0,2000\n"
              "2007-01-04,101.5,103.0,101.0,102.0,3000\n");
    dir.write("BBB.csv",
              "date,open,high,low,close,volume\n"
              "2007-01-03,49.5,51.0,49.0,50.0,500\n"
              "2007-01-04,51.5,53.0,51.0,52.0,600\n");

    DataHandler data = DataHandler::from_csv_directory(dir.path(), {"AAA", "BBB"});
    EXPECT_EQ(data.timeline_size(), 3u);

    std::size_t steps = 0;
    while (data.has_more_bars()) {
        data.advance();
        ++steps;
    }
    EXPECT_EQ(steps, 3u);
    EXPECT_EQ(data.current_time(), Timestamp{2007y / January / 4});
    EXPECT_DOUBLE_EQ(data.latest_bars("BBB", 1)[0].close, 52.0);
}
