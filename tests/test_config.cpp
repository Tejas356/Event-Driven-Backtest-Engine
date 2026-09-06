#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "backtest/config.hpp"
#include "backtest/results.hpp"

using namespace backtest;
namespace fs = std::filesystem;

namespace {

class TempDir {
public:
    TempDir()
        : path_(fs::temp_directory_path() /
                fs::path("tsmom_cfg_" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }
    fs::path write(const std::string& name, const std::string& contents) const {
        std::ofstream out(path_ / name);
        out << contents;
        return path_ / name;
    }

private:
    fs::path path_;
};

}  // namespace

TEST(Config, ParsesKeyValueLinesAndIgnoresCommentsAndBlanks) {
    TempDir dir;
    const fs::path file = dir.write("test.toml",
                                    "# a comment\n"
                                    "\n"
                                    "lookback = 126   # trailing comment\n"
                                    "volatility_target=0.15\n"
                                    "monthly_rebalance = false\n"
                                    "symbols = SPY, TLT ,GLD\n"
                                    "commission_bps = 1.25\n");

    const BacktestConfig config = load_config(file);

    EXPECT_EQ(config.lookback, 126u);
    EXPECT_DOUBLE_EQ(config.volatility_target, 0.15);
    EXPECT_FALSE(config.monthly_rebalance);
    EXPECT_EQ(config.symbols, (std::vector<Symbol>{"SPY", "TLT", "GLD"}));
    EXPECT_DOUBLE_EQ(config.costs.commission_bps, 1.25);
    // Untouched keys keep their defaults.
    EXPECT_DOUBLE_EQ(config.metrics.risk_free_rate, 0.02);
}

TEST(Config, AnUnknownKeyIsAnErrorRatherThanBeingIgnored) {
    // A silently ignored typo in a parameter name is a run that did not do
    // what its config says, which is worse than a run that refuses to start.
    TempDir dir;
    EXPECT_THROW(load_config(dir.write("bad.toml", "lookbcak = 252\n")), std::runtime_error);
    EXPECT_THROW(load_config(dir.write("nosep.toml", "lookback 252\n")), std::runtime_error);
    EXPECT_THROW(load_config(dir.write("nan.toml", "lookback = many\n")), std::runtime_error);
    EXPECT_THROW(load_config(dir.write("bool.toml", "monthly_rebalance = maybe\n")),
                 std::runtime_error);
    EXPECT_THROW(load_config(dir.path() / "missing.toml"), std::runtime_error);
}

TEST(Config, CommandLineOverridesTakePrecedence) {
    BacktestConfig config;
    config.lookback = 252;

    const BacktestConfig overridden =
        apply_overrides(config, {"--lookback=63", "--volatility_target=0.2"});

    EXPECT_EQ(overridden.lookback, 63u);
    EXPECT_DOUBLE_EQ(overridden.volatility_target, 0.2);
    EXPECT_THROW(apply_overrides(config, {"lookback=63"}), std::runtime_error);
    EXPECT_THROW(apply_overrides(config, {"--lookback"}), std::runtime_error);
}

TEST(Config, TheShippedDefaultConfigParses) {
    // The file in config/ is part of the interface; a typo in it would only
    // show up when someone tried to run the project.
    const fs::path shipped = fs::path{TSMOM_DATA_DIR}.parent_path() / "config" / "default.toml";
    if (!fs::exists(shipped)) {
        GTEST_SKIP() << "no config/default.toml at " << shipped;
    }
    const BacktestConfig config = load_config(shipped);
    EXPECT_EQ(config.symbols.size(), 8u);
    EXPECT_EQ(config.lookback, 252u);
    EXPECT_TRUE(config.monthly_rebalance);
}

TEST(Config, FormatsDatesAsIso) {
    using namespace std::chrono;
    EXPECT_EQ(to_iso_date(Timestamp{2007y / January / 3}), "2007-01-03");
    EXPECT_EQ(to_iso_date(Timestamp{2026y / December / 31}), "2026-12-31");
}

// --------------------------------------------------------------------------
// Results output
// --------------------------------------------------------------------------

#include "backtest/engine.hpp"
#include "buy_and_hold.hpp"

namespace {

std::string read_file(const fs::path& file) {
    std::ifstream in(file);
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::size_t count_lines(const std::string& text) {
    return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
}

}  // namespace

TEST(Results, WritesAllFourFilesWithConsistentContents) {
    using namespace std::chrono;

    std::map<Symbol, std::vector<Bar>> bars;
    std::vector<Bar> series;
    for (int i = 0; i < 10; ++i) {
        const double close = 100.0 + i;
        series.push_back(Bar{.ts = Timestamp{2007y / January / 2} + days{i},
                             .open = close,
                             .high = close,
                             .low = close,
                             .close = close,
                             .volume = 1000});
    }
    bars.emplace("AAA", series);

    DataHandler data = DataHandler::from_bars(bars);
    BuyAndHoldStrategy strategy{"AAA"};
    Portfolio portfolio{PortfolioConfig{.initial_capital = 100000.0}};
    ExecutionHandler execution;
    Engine(data, strategy, portfolio, execution).run();
    const BacktestMetrics metrics = compute_metrics(portfolio);

    TempDir dir;
    BacktestConfig config;
    config.symbols = {"AAA"};
    write_results(dir.path(), config, portfolio, metrics);

    for (const char* name :
         {"equity_curve.csv", "trades.csv", "positions.csv", "metrics.json"}) {
        EXPECT_TRUE(fs::exists(dir.path() / name)) << name << " was not written";
    }

    // One header plus one row per bar.
    const std::string equity = read_file(dir.path() / "equity_curve.csv");
    EXPECT_EQ(count_lines(equity), portfolio.equity_curve().size() + 1);
    EXPECT_NE(equity.find("date,equity,gross_equity,gross_exposure,net_exposure"),
              std::string::npos);
    EXPECT_NE(equity.find("2007-01-02"), std::string::npos);

    const std::string trades = read_file(dir.path() / "trades.csv");
    EXPECT_EQ(count_lines(trades), portfolio.fills().size() + 1);

    const std::string positions = read_file(dir.path() / "positions.csv");
    EXPECT_EQ(count_lines(positions), portfolio.position_history().size() + 1);

    // metrics.json records the configuration next to the result, because a
    // metrics file that does not say what produced it is not reproducible.
    const std::string json = read_file(dir.path() / "metrics.json");
    EXPECT_NE(json.find("\"config\""), std::string::npos);
    EXPECT_NE(json.find("\"lookback\": 252"), std::string::npos);
    EXPECT_NE(json.find("\"gross\""), std::string::npos);
    EXPECT_NE(json.find("\"net\""), std::string::npos);
    EXPECT_NE(json.find("\"annualised_turnover\""), std::string::npos);
    EXPECT_NE(json.find("\"AAA\""), std::string::npos);
}

TEST(Results, MetricsTableShowsGrossBesideNetAndBothSharpes) {
    BacktestConfig config;
    BacktestMetrics metrics;
    metrics.net.annualised_volatility = 0.05;
    metrics.net.sharpe = 0.01;
    metrics.gross.annualised_volatility = 0.05;
    metrics.gross.sharpe = 0.02;

    const std::string table = format_metrics_table(config, metrics);

    EXPECT_NE(table.find("gross"), std::string::npos);
    EXPECT_NE(table.find("net"), std::string::npos);
    EXPECT_NE(table.find("sharpe (no risk-free)"), std::string::npos);
    EXPECT_NE(table.find("drawdown duration (days)"), std::string::npos);
    EXPECT_NE(table.find("annualised turnover"), std::string::npos);

    // 0.01 + 0.02/0.05 = 0.41
    EXPECT_NEAR(sharpe_without_risk_free(metrics.net, config.metrics.risk_free_rate), 0.41, 1e-12);
}
