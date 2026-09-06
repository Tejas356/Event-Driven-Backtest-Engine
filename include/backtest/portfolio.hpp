#pragma once

#include <map>
#include <optional>
#include <vector>

#include "backtest/event.hpp"
#include "backtest/types.hpp"

namespace backtest {

// One row of the equity curve.
struct EquityPoint {
    Timestamp ts;
    double equity = 0.0;
    // Equity the same positions would have reached having paid no commission
    // or slippage. Reported beside the net figure so the cost drag is visible
    // rather than buried in a summary statistic.
    double gross_equity = 0.0;
    // Sum of absolute position values over equity, and the signed sum. Gross
    // exposure is what the leverage cap constrains; net is the directional
    // bet, which for a long/short trend book is usually far smaller.
    double gross_exposure = 0.0;
    double net_exposure = 0.0;
};

struct PositionRecord {
    Timestamp ts;
    Symbol symbol;
    Quantity quantity = 0.0;
    double weight = 0.0;
};

struct PortfolioConfig {
    double initial_capital = 1000000.0;
    // Orders smaller than this fraction of equity are not sent. Rebalancing
    // to the last basis point costs turnover and buys nothing, so a deadband
    // is a real technique rather than a shortcut -- but it is a parameter,
    // and it is logged as one.
    double min_trade_fraction = 0.0005;
};

// Holds positions, cash and the equity curve, and turns target weights into
// orders. Sizing lives here rather than in the strategy so that a strategy
// never needs to know the account size.
class Portfolio {
public:
    explicit Portfolio(PortfolioConfig config = {});

    // Records the latest price for a symbol. Called on every market event.
    void mark_to_market(const MarketEvent& event);

    // Converts a target weight into the order that would reach it, or
    // nullopt when the gap is inside the deadband or the price is unknown.
    std::optional<OrderEvent> on_signal(const SignalEvent& signal);

    void on_fill(const FillEvent& fill);

    // Appends one row to the equity curve. Called once per timestamp.
    void record_equity(Timestamp ts);

    double cash() const noexcept { return cash_; }
    double equity() const;
    double gross_equity() const { return equity() + total_costs(); }
    Quantity position(const Symbol& symbol) const;
    std::optional<Price> last_price(const Symbol& symbol) const;

    double total_commission() const noexcept { return total_commission_; }
    double total_slippage() const noexcept { return total_slippage_; }
    double total_costs() const noexcept { return total_commission_ + total_slippage_; }
    // Sum of absolute traded notional, the numerator of turnover.
    double total_traded_notional() const noexcept { return total_traded_notional_; }

    const std::vector<EquityPoint>& equity_curve() const noexcept { return equity_curve_; }
    const std::vector<FillEvent>& fills() const noexcept { return fills_; }
    const std::vector<PositionRecord>& position_history() const noexcept {
        return position_history_;
    }
    const PortfolioConfig& config() const noexcept { return config_; }

private:
    PortfolioConfig config_;
    double cash_;
    std::map<Symbol, Quantity> positions_;
    std::map<Symbol, Price> last_prices_;

    double total_commission_ = 0.0;
    double total_slippage_ = 0.0;
    double total_traded_notional_ = 0.0;

    std::vector<EquityPoint> equity_curve_;
    std::vector<FillEvent> fills_;
    std::vector<PositionRecord> position_history_;
};

}  // namespace backtest
