#include "backtest/config.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace backtest {
namespace {

std::string trim(std::string text) {
    const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
    return text;
}

double to_double(const std::string& key, const std::string& value) {
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0') {
        throw std::runtime_error("config: " + key + " is not a number: \"" + value + "\"");
    }
    return parsed;
}

std::size_t to_size(const std::string& key, const std::string& value) {
    const double parsed = to_double(key, value);
    if (parsed < 0.0) {
        throw std::runtime_error("config: " + key + " must not be negative");
    }
    return static_cast<std::size_t>(parsed);
}

bool to_bool(const std::string& key, const std::string& value) {
    if (value == "true" || value == "1" || value == "yes") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no") {
        return false;
    }
    throw std::runtime_error("config: " + key + " is not a boolean: \"" + value + "\"");
}

std::vector<Symbol> to_symbols(const std::string& value) {
    std::vector<Symbol> symbols;
    std::istringstream stream(value);
    std::string symbol;
    while (std::getline(stream, symbol, ',')) {
        symbol = trim(symbol);
        if (!symbol.empty()) {
            symbols.push_back(symbol);
        }
    }
    return symbols;
}

// An unknown key is an error rather than a warning. A silently ignored
// typo in a parameter name is a run that did not do what its config says,
// which is worse than a run that refuses to start.
void assign(BacktestConfig& config, const std::string& key, const std::string& value) {
    if (key == "data_dir") {
        config.data_dir = value;
    } else if (key == "output_dir") {
        config.output_dir = value;
    } else if (key == "symbols") {
        config.symbols = to_symbols(value);
    } else if (key == "lookback") {
        config.lookback = to_size(key, value);
    } else if (key == "volatility_halflife") {
        config.volatility_halflife = to_double(key, value);
    } else if (key == "volatility_seed") {
        config.volatility_seed = to_size(key, value);
    } else if (key == "volatility_target") {
        config.volatility_target = to_double(key, value);
    } else if (key == "max_gross_leverage") {
        config.max_gross_leverage = to_double(key, value);
    } else if (key == "monthly_rebalance") {
        config.monthly_rebalance = to_bool(key, value);
    } else if (key == "initial_capital") {
        config.initial_capital = to_double(key, value);
    } else if (key == "min_trade_fraction") {
        config.min_trade_fraction = to_double(key, value);
    } else if (key == "commission_bps") {
        config.costs.commission_bps = to_double(key, value);
    } else if (key == "slippage_bps") {
        config.costs.slippage_bps = to_double(key, value);
    } else if (key == "risk_free_rate") {
        config.metrics.risk_free_rate = to_double(key, value);
    } else {
        throw std::runtime_error("config: unknown key \"" + key + "\"");
    }
}

}  // namespace

BacktestConfig load_config(const std::filesystem::path& file, BacktestConfig defaults) {
    std::ifstream input(file);
    if (!input) {
        throw std::runtime_error("could not open config file: " + file.string());
    }

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error(file.string() + ":" + std::to_string(line_number) +
                                     ": expected key = value");
        }
        assign(defaults, trim(line.substr(0, equals)), trim(line.substr(equals + 1)));
    }
    return defaults;
}

BacktestConfig apply_overrides(BacktestConfig config, const std::vector<std::string>& arguments) {
    for (const std::string& argument : arguments) {
        if (argument.rfind("--", 0) != 0) {
            throw std::runtime_error("unexpected argument: " + argument);
        }
        const std::string body = argument.substr(2);
        const std::size_t equals = body.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("expected --key=value, got: " + argument);
        }
        assign(config, body.substr(0, equals), body.substr(equals + 1));
    }
    return config;
}

std::string to_iso_date(Timestamp ts) {
    const std::chrono::year_month_day ymd{ts};
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << static_cast<int>(ymd.year()) << '-' << std::setw(2)
        << static_cast<unsigned>(ymd.month()) << '-' << std::setw(2)
        << static_cast<unsigned>(ymd.day());
    return out.str();
}

}  // namespace backtest
