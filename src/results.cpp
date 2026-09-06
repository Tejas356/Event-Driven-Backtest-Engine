#include "backtest/results.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace backtest {
namespace {

std::ofstream open_output(const std::filesystem::path& file) {
    std::ofstream out(file);
    if (!out) {
        throw std::runtime_error("could not write " + file.string());
    }
    // Enough digits to round-trip a double, so re-reading a results file
    // cannot introduce a difference that looks like a bug.
    out << std::setprecision(17);
    return out;
}

void write_metric_group(std::ostream& out, const char* name, const PerformanceMetrics& metrics,
                        const char* indent) {
    out << indent << '"' << name << "\": {\n"
        << indent << "  \"annualised_return\": " << metrics.annualised_return << ",\n"
        << indent << "  \"annualised_volatility\": " << metrics.annualised_volatility << ",\n"
        << indent << "  \"sharpe\": " << metrics.sharpe << ",\n"
        << indent << "  \"max_drawdown\": " << metrics.max_drawdown << ",\n"
        << indent << "  \"max_drawdown_days\": " << metrics.max_drawdown_days << ",\n"
        << indent << "  \"skewness\": " << metrics.skewness << ",\n"
        << indent << "  \"excess_kurtosis\": " << metrics.excess_kurtosis << ",\n"
        << indent << "  \"observations\": " << metrics.observations << "\n"
        << indent << "}";
}

}  // namespace

void write_results(const std::filesystem::path& directory, const BacktestConfig& config,
                   const Portfolio& portfolio, const BacktestMetrics& metrics) {
    std::filesystem::create_directories(directory);

    {
        std::ofstream out = open_output(directory / "equity_curve.csv");
        out << "date,equity,gross_equity,gross_exposure,net_exposure\n";
        for (const EquityPoint& point : portfolio.equity_curve()) {
            out << to_iso_date(point.ts) << ',' << point.equity << ',' << point.gross_equity << ','
                << point.gross_exposure << ',' << point.net_exposure << '\n';
        }
    }

    {
        // Costs are broken out per fill rather than netted, so the drag can
        // be attributed rather than merely totalled.
        std::ofstream out = open_output(directory / "trades.csv");
        out << "date,symbol,quantity,fill_price,notional,commission,slippage\n";
        for (const FillEvent& fill : portfolio.fills()) {
            out << to_iso_date(fill.ts) << ',' << fill.symbol << ',' << fill.quantity << ','
                << fill.fill_price << ',' << (fill.quantity * fill.fill_price) << ','
                << fill.commission << ',' << fill.slippage << '\n';
        }
    }

    {
        std::ofstream out = open_output(directory / "positions.csv");
        out << "date,symbol,quantity,weight\n";
        for (const PositionRecord& record : portfolio.position_history()) {
            out << to_iso_date(record.ts) << ',' << record.symbol << ',' << record.quantity << ','
                << record.weight << '\n';
        }
    }

    {
        std::ofstream out = open_output(directory / "metrics.json");
        out << "{\n  \"config\": {\n"
            << "    \"symbols\": [";
        for (std::size_t i = 0; i < config.symbols.size(); ++i) {
            out << (i == 0 ? "" : ", ") << '"' << config.symbols[i] << '"';
        }
        out << "],\n"
            << "    \"lookback\": " << config.lookback << ",\n"
            << "    \"volatility_halflife\": " << config.volatility_halflife << ",\n"
            << "    \"volatility_target\": " << config.volatility_target << ",\n"
            << "    \"max_gross_leverage\": " << config.max_gross_leverage << ",\n"
            << "    \"monthly_rebalance\": " << (config.monthly_rebalance ? "true" : "false")
            << ",\n"
            << "    \"initial_capital\": " << config.initial_capital << ",\n"
            << "    \"min_trade_fraction\": " << config.min_trade_fraction << ",\n"
            << "    \"commission_bps\": " << config.costs.commission_bps << ",\n"
            << "    \"slippage_bps\": " << config.costs.slippage_bps << ",\n"
            << "    \"risk_free_rate\": " << config.metrics.risk_free_rate << "\n"
            << "  },\n";
        write_metric_group(out, "gross", metrics.gross, "  ");
        out << ",\n";
        write_metric_group(out, "net", metrics.net, "  ");
        out << ",\n"
            << "  \"annualised_turnover\": " << metrics.annualised_turnover << ",\n"
            << "  \"total_commission\": " << metrics.total_commission << ",\n"
            << "  \"total_slippage\": " << metrics.total_slippage << ",\n"
            << "  \"cost_drag\": " << metrics.cost_drag << ",\n"
            << "  \"years\": " << metrics.years << ",\n"
            << "  \"initial_equity\": " << metrics.initial_equity << ",\n"
            << "  \"final_equity\": " << metrics.final_equity << "\n}\n";
    }
}

std::string format_metrics_table(const BacktestConfig& config, const BacktestMetrics& metrics) {
    std::ostringstream out;
    out << std::fixed;

    const auto row = [&](const char* label, double gross, double net, int precision) {
        out << "  " << std::left << std::setw(26) << label << std::right << std::setprecision(precision)
            << std::setw(14) << gross << std::setw(14) << net << '\n';
    };

    out << "  " << std::left << std::setw(26) << "metric" << std::right << std::setw(14) << "gross"
        << std::setw(14) << "net" << '\n';
    out << "  " << std::string(54, '-') << '\n';

    row("annualised return", metrics.gross.annualised_return, metrics.net.annualised_return, 4);
    row("annualised volatility", metrics.gross.annualised_volatility,
        metrics.net.annualised_volatility, 4);
    row("sharpe", metrics.gross.sharpe, metrics.net.sharpe, 4);
    row("sharpe (no risk-free)", sharpe_without_risk_free(metrics.gross, config.metrics.risk_free_rate),
        sharpe_without_risk_free(metrics.net, config.metrics.risk_free_rate), 4);
    row("max drawdown", metrics.gross.max_drawdown, metrics.net.max_drawdown, 4);
    row("drawdown duration (days)", static_cast<double>(metrics.gross.max_drawdown_days),
        static_cast<double>(metrics.net.max_drawdown_days), 0);
    row("skew", metrics.gross.skewness, metrics.net.skewness, 4);
    row("excess kurtosis", metrics.gross.excess_kurtosis, metrics.net.excess_kurtosis, 4);

    out << "  " << std::string(54, '-') << '\n';
    out << std::setprecision(4);
    out << "  annualised turnover        " << metrics.annualised_turnover << '\n';
    out << "  cost drag (return)         " << metrics.cost_drag << '\n';
    out << std::setprecision(2);
    out << "  commission / slippage      " << metrics.total_commission << " / "
        << metrics.total_slippage << '\n';
    out << "  equity                     " << metrics.initial_equity << " -> "
        << metrics.final_equity << '\n';
    out << std::setprecision(2);
    out << "  years                      " << metrics.years << '\n';
    out << "\n  Two Sharpe figures are shown because the engine credits no interest\n"
           "  on idle cash. The risk-free-adjusted row charges the strategy for a\n"
           "  cash return it never earned; the row below it charges nothing. The\n"
           "  truth is between them, nearer the lower row the more of the book sits\n"
           "  in cash.\n";
    return out.str();
}

}  // namespace backtest
