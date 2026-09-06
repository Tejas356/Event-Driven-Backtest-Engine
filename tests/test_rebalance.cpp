#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <vector>

#include "backtest/engine.hpp"
#include "backtest/metrics.hpp"
#include "tsmom.hpp"

using namespace backtest;
using namespace std::chrono;

namespace {

const std::filesystem::path kDataDir{TSMOM_DATA_DIR};
const std::vector<Symbol> kUniverse{"DBC", "EEM", "EFA", "GLD", "IEF", "SPY", "TLT", "UUP"};

bool universe_available() {
    for (const Symbol& symbol : kUniverse) {
        if (!std::filesystem::exists(kDataDir / (symbol + ".csv"))) {
            return false;
        }
    }
    return true;
}

// Weekdays only, so the fixture has month ends that fall on a Friday rather
// than on the 30th or 31st -- which is the case a naive calendar check gets
// wrong.
std::vector<Bar> weekdays(Timestamp from, Timestamp to) {
    std::vector<Bar> bars;
    for (Timestamp ts = from; ts <= to; ts += days{1}) {
        const weekday wd{ts};
        if (wd == Saturday || wd == Sunday) {
            continue;
        }
        bars.push_back(Bar{.ts = ts, .open = 100.0, .high = 100.0, .low = 100.0,
                           .close = 100.0, .volume = 1000});
    }
    return bars;
}

}  // namespace

TEST(MonthEnd, IdentifiesTheLastTradingDayNotTheLastCalendarDay) {
    // March 2007 ended on Saturday the 31st, so the last trading day is
    // Friday the 30th. A check for "day == 31" would miss it entirely.
    DataHandler data = DataHandler::from_bars(
        {{"AAA", weekdays(Timestamp{2007y / March / 26}, Timestamp{2007y / April / 6})}});

    std::vector<Timestamp> month_ends;
    while (data.has_more_bars()) {
        data.advance();
        if (data.is_last_trading_day_of_month()) {
            month_ends.push_back(data.current_time());
        }
    }

    // Friday 30 March, plus the final bar of the sample which ends its month
    // by definition.
    ASSERT_EQ(month_ends.size(), 2u);
    EXPECT_EQ(month_ends[0], Timestamp{2007y / March / 30});
    EXPECT_EQ(month_ends[1], Timestamp{2007y / April / 6});
}

TEST(MonthEnd, FlagsExactlyOneDayPerMonthOnRealData) {
    if (!universe_available()) {
        GTEST_SKIP() << "no CSVs in " << kDataDir << "; run python data/fetch_data.py";
    }

    DataHandler data = DataHandler::from_csv_directory(kDataDir, {"SPY"});

    std::set<std::pair<int, unsigned>> months;
    std::set<std::pair<int, unsigned>> flagged;
    std::size_t flags = 0;

    while (data.has_more_bars()) {
        data.advance();
        const year_month_day ymd{data.current_time()};
        const std::pair<int, unsigned> key{static_cast<int>(ymd.year()),
                                           static_cast<unsigned>(ymd.month())};
        months.insert(key);
        if (data.is_last_trading_day_of_month()) {
            ++flags;
            // Never twice in the same month.
            EXPECT_TRUE(flagged.insert(key).second) << "month flagged twice";
        }
    }

    // Every month in the sample is flagged exactly once.
    EXPECT_EQ(flags, months.size());
    EXPECT_EQ(flagged.size(), months.size());
    EXPECT_GT(months.size(), 200u) << "expected roughly 20 years of months";
}

TEST(MonthlyRebalance, RebalanceCountEqualsTheNumberOfMonthsInTheSample) {
    if (!universe_available()) {
        GTEST_SKIP() << "no CSVs in " << kDataDir << "; run python data/fetch_data.py";
    }

    DataHandler data = DataHandler::from_csv_directory(kDataDir, kUniverse);

    // Count the months independently of the strategy.
    std::set<std::pair<int, unsigned>> months;
    {
        DataHandler counter = DataHandler::from_csv_directory(kDataDir, kUniverse);
        while (counter.has_more_bars()) {
            counter.advance();
            const year_month_day ymd{counter.current_time()};
            months.insert({static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month())});
        }
    }

    TsmomStrategy strategy{kUniverse, TsmomConfig{.frequency = RebalanceFrequency::MonthEnd}};
    Portfolio portfolio;
    ExecutionHandler execution;
    Engine(data, strategy, portfolio, execution).run();

    EXPECT_EQ(strategy.rebalance_count(), months.size());
}

TEST(MonthlyRebalance, NoOrdersAreGeneratedBetweenRebalanceDates) {
    if (!universe_available()) {
        GTEST_SKIP() << "no CSVs in " << kDataDir << "; run python data/fetch_data.py";
    }

    // Collect the dates that are month ends, then confirm every fill happens
    // on the bar immediately after one of them -- fills lag their order by a
    // bar, so a month-end order fills on the first trading day of the next
    // month and nowhere else.
    std::vector<Timestamp> timeline;
    std::vector<bool> month_end;
    {
        DataHandler calendar = DataHandler::from_csv_directory(kDataDir, kUniverse);
        while (calendar.has_more_bars()) {
            calendar.advance();
            timeline.push_back(calendar.current_time());
            month_end.push_back(calendar.is_last_trading_day_of_month());
        }
    }

    std::set<Timestamp> allowed_fill_dates;
    for (std::size_t i = 0; i + 1 < timeline.size(); ++i) {
        if (month_end[i]) {
            allowed_fill_dates.insert(timeline[i + 1]);
        }
    }

    DataHandler data = DataHandler::from_csv_directory(kDataDir, kUniverse);
    TsmomStrategy strategy{kUniverse, TsmomConfig{.frequency = RebalanceFrequency::MonthEnd}};
    Portfolio portfolio;
    ExecutionHandler execution;
    Engine(data, strategy, portfolio, execution).run();

    ASSERT_FALSE(portfolio.fills().empty());
    for (const FillEvent& fill : portfolio.fills()) {
        EXPECT_TRUE(allowed_fill_dates.count(fill.ts) > 0)
            << "fill on " << fill.ts << " is not the day after a month end";
    }
}

TEST(MonthlyRebalance, CutsTurnoverSeveralFoldAgainstDailyRebalancing) {
    if (!universe_available()) {
        GTEST_SKIP() << "no CSVs in " << kDataDir << "; run python data/fetch_data.py";
    }

    const auto turnover_for = [](RebalanceFrequency frequency) {
        DataHandler data = DataHandler::from_csv_directory(kDataDir, kUniverse);
        TsmomStrategy strategy{kUniverse, TsmomConfig{.frequency = frequency}};
        Portfolio portfolio{PortfolioConfig{.initial_capital = 1000000.0}};
        ExecutionHandler execution;
        Engine(data, strategy, portfolio, execution).run();
        return compute_metrics(portfolio).annualised_turnover;
    };

    const double daily = turnover_for(RebalanceFrequency::Daily);
    const double monthly = turnover_for(RebalanceFrequency::MonthEnd);

    // Being able to demonstrate this is the reason the schedule is a
    // parameter rather than a hard-coded month end.
    EXPECT_GT(daily, monthly * 3.0)
        << "daily " << daily << " vs monthly " << monthly;
}
