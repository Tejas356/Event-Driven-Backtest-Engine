#include "backtest/data_handler.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace backtest {
namespace {

using std::filesystem::path;

[[noreturn]] void fail(const path& file, std::size_t line, const std::string& what) {
    throw std::runtime_error(file.string() + ":" + std::to_string(line) + ": " + what);
}

// Trailing \r from a CRLF file, and a UTF-8 BOM on the first line, are the two
// ways a perfectly valid CSV fails to parse on Windows. Handle both rather
// than emitting a confusing error about the date field.
void strip_line_noise(std::string& line, bool first_line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (first_line && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
        line.erase(0, 3);
    }
}

std::vector<std::string> split_fields(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream stream(line);
    while (std::getline(stream, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

int parse_int(const std::string& text, const path& file, std::size_t line, const char* what) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') {
        fail(file, line, std::string("could not parse ") + what + " from \"" + text + "\"");
    }
    return static_cast<int>(value);
}

// Strict YYYY-MM-DD. A looser parse would happily accept a reordered or
// partially garbled date and misalign an entire series.
Timestamp parse_date(const std::string& text, const path& file, std::size_t line) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        fail(file, line, "expected a YYYY-MM-DD date, got \"" + text + "\"");
    }
    const int year = parse_int(text.substr(0, 4), file, line, "year");
    const int month = parse_int(text.substr(5, 2), file, line, "month");
    const int day = parse_int(text.substr(8, 2), file, line, "day");

    const std::chrono::year_month_day ymd{std::chrono::year{year},
                                          std::chrono::month{static_cast<unsigned>(month)},
                                          std::chrono::day{static_cast<unsigned>(day)}};
    if (!ymd.ok()) {
        fail(file, line, "not a real calendar date: \"" + text + "\"");
    }
    return Timestamp{ymd};
}

double parse_price(const std::string& text, const path& file, std::size_t line, const char* what) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') {
        fail(file, line, std::string("could not parse ") + what + " from \"" + text + "\"");
    }
    if (!(value > 0.0)) {
        fail(file, line, std::string(what) + " must be positive, got \"" + text + "\"");
    }
    return value;
}

}  // namespace

std::vector<Bar> read_bars_csv(const path& file) {
    std::ifstream input(file);
    if (!input) {
        throw std::runtime_error("could not open data file: " + file.string());
    }

    std::vector<Bar> bars;
    std::string line;
    std::size_t line_number = 0;
    bool header_seen = false;

    while (std::getline(input, line)) {
        ++line_number;
        strip_line_noise(line, line_number == 1);
        if (line.empty()) {
            continue;
        }
        if (!header_seen) {
            header_seen = true;
            // Skip the header row if present; a numeric first field means the
            // file has no header and this row is data.
            if (!line.empty() && (std::isalpha(static_cast<unsigned char>(line[0])) != 0)) {
                continue;
            }
        }

        const std::vector<std::string> fields = split_fields(line);
        if (fields.size() < 6) {
            fail(file, line_number,
                 "expected 6 fields (date,open,high,low,close,volume), got " +
                     std::to_string(fields.size()));
        }

        Bar bar{};
        bar.ts = parse_date(fields[0], file, line_number);
        bar.open = parse_price(fields[1], file, line_number, "open");
        bar.high = parse_price(fields[2], file, line_number, "high");
        bar.low = parse_price(fields[3], file, line_number, "low");
        bar.close = parse_price(fields[4], file, line_number, "close");
        bar.volume = std::strtol(fields[5].c_str(), nullptr, 10);

        if (bar.high < bar.low) {
            fail(file, line_number, "high is below low");
        }
        if (!bars.empty() && bar.ts <= bars.back().ts) {
            fail(file, line_number, "dates must be strictly ascending");
        }
        bars.push_back(bar);
    }

    if (bars.empty()) {
        throw std::runtime_error("no bars found in " + file.string());
    }
    return bars;
}

DataHandler::DataHandler(std::vector<Timestamp> timeline, std::vector<Symbol> symbols,
                         std::map<Symbol, SymbolSeries> series)
    : timeline_(std::move(timeline)), symbols_(std::move(symbols)), series_(std::move(series)) {}

DataHandler DataHandler::from_bars(std::map<Symbol, std::vector<Bar>> bars_by_symbol) {
    if (bars_by_symbol.empty()) {
        throw std::invalid_argument("DataHandler: no symbols supplied");
    }

    // Union of every symbol timeline, so that a date on which only some
    // symbols traded is still a simulation step for all of them.
    std::set<Timestamp> all_dates;
    std::vector<Symbol> symbols;
    for (const auto& [symbol, bars] : bars_by_symbol) {
        if (bars.empty()) {
            throw std::invalid_argument("DataHandler: no bars for symbol " + symbol);
        }
        for (std::size_t i = 1; i < bars.size(); ++i) {
            if (bars[i].ts <= bars[i - 1].ts) {
                throw std::invalid_argument("DataHandler: bars for " + symbol +
                                            " are not in strictly ascending date order");
            }
        }
        symbols.push_back(symbol);
        for (const Bar& bar : bars) {
            all_dates.insert(bar.ts);
        }
    }

    const std::vector<Timestamp> timeline(all_dates.begin(), all_dates.end());

    std::map<Symbol, SymbolSeries> series;
    for (auto& [symbol, bars] : bars_by_symbol) {
        SymbolSeries built;
        const Timestamp first_date = bars.front().ts;
        built.start_index = static_cast<std::size_t>(
            std::lower_bound(timeline.begin(), timeline.end(), first_date) - timeline.begin());

        built.bars.reserve(timeline.size() - built.start_index);
        built.traded.reserve(timeline.size() - built.start_index);

        std::size_t next_bar = 0;
        for (std::size_t t = built.start_index; t < timeline.size(); ++t) {
            if (next_bar < bars.size() && bars[next_bar].ts == timeline[t]) {
                built.bars.push_back(bars[next_bar]);
                built.traded.push_back(true);
                ++next_bar;
            } else {
                // Forward-fill: carry the last known price forward, stamped
                // with the current date so the bar cannot claim to be more
                // recent than it is. Marked untraded so advance() does not
                // emit it as a market event.
                Bar filled = built.bars.back();
                filled.ts = timeline[t];
                filled.open = filled.close;
                filled.high = filled.close;
                filled.low = filled.close;
                filled.volume = 0;
                built.bars.push_back(filled);
                built.traded.push_back(false);
            }
        }
        series.emplace(symbol, std::move(built));
    }

    return DataHandler(timeline, std::move(symbols), std::move(series));
}

DataHandler DataHandler::from_csv_directory(const path& directory,
                                            const std::vector<Symbol>& symbols) {
    if (symbols.empty()) {
        throw std::invalid_argument("DataHandler: no symbols requested");
    }
    std::map<Symbol, std::vector<Bar>> bars_by_symbol;
    for (const Symbol& symbol : symbols) {
        bars_by_symbol.emplace(symbol, read_bars_csv(directory / (symbol + ".csv")));
    }
    return from_bars(std::move(bars_by_symbol));
}

bool DataHandler::has_more_bars() const noexcept {
    return cursor_ == kBeforeStart ? !timeline_.empty() : cursor_ + 1 < timeline_.size();
}

std::vector<MarketEvent> DataHandler::advance() {
    if (!has_more_bars()) {
        throw std::logic_error("DataHandler::advance called past the end of the timeline");
    }
    cursor_ = (cursor_ == kBeforeStart) ? 0 : cursor_ + 1;

    const Timestamp now = timeline_[cursor_];
    std::vector<MarketEvent> events;
    events.reserve(symbols_.size());
    for (const Symbol& symbol : symbols_) {
        const SymbolSeries& s = series_.at(symbol);
        if (cursor_ < s.start_index) {
            continue;  // this symbol has no history yet
        }
        const std::size_t offset = cursor_ - s.start_index;
        if (!s.traded[offset]) {
            continue;  // forward-filled placeholder, not a real bar
        }
        events.push_back(MarketEvent{now, symbol, s.bars[offset]});
    }
    return events;
}

std::span<const Bar> DataHandler::latest_bars(const Symbol& symbol, std::size_t n) const {
    if (cursor_ == kBeforeStart || n == 0) {
        return {};
    }
    const auto it = series_.find(symbol);
    if (it == series_.end()) {
        return {};
    }
    const SymbolSeries& s = it->second;
    if (cursor_ < s.start_index) {
        return {};
    }

    // The clamp that makes look-ahead impossible: available counts only bars
    // up to and including the cursor, so the span can never reach past it.
    const std::size_t available = cursor_ - s.start_index + 1;
    const std::size_t count = std::min(n, available);
    return std::span<const Bar>(s.bars.data() + (available - count), count);
}

bool DataHandler::traded(const Symbol& symbol) const {
    if (cursor_ == kBeforeStart) {
        return false;
    }
    const auto it = series_.find(symbol);
    if (it == series_.end() || cursor_ < it->second.start_index) {
        return false;
    }
    return it->second.traded[cursor_ - it->second.start_index];
}

bool DataHandler::is_last_trading_day_of_month() const {
    if (cursor_ == kBeforeStart || timeline_.empty()) {
        return false;
    }
    // The final bar of the sample ends its month by definition.
    if (cursor_ + 1 >= timeline_.size()) {
        return true;
    }
    // Asking the calendar for the 31st would miss every month ending on a
    // weekend or a holiday, which is most of them. The last trading day is
    // the one whose successor falls in a different month.
    const std::chrono::year_month_day today{timeline_[cursor_]};
    const std::chrono::year_month_day tomorrow{timeline_[cursor_ + 1]};
    return today.month() != tomorrow.month() || today.year() != tomorrow.year();
}

Timestamp DataHandler::current_time() const {
    if (cursor_ == kBeforeStart) {
        throw std::logic_error("DataHandler::current_time called before the first advance");
    }
    return timeline_[cursor_];
}

}  // namespace backtest
