#include "backtest/portfolio.hpp"

#include <cmath>
#include <stdexcept>

namespace backtest {

Portfolio::Portfolio(PortfolioConfig config)
    : config_(config), cash_(config.initial_capital) {
    if (!(config_.initial_capital > 0.0)) {
        throw std::invalid_argument("Portfolio: initial capital must be positive");
    }
    if (config_.min_trade_fraction < 0.0) {
        throw std::invalid_argument("Portfolio: min trade fraction must not be negative");
    }
}

void Portfolio::mark_to_market(const MarketEvent& event) {
    last_prices_[event.symbol] = event.bar.close;
}

double Portfolio::equity() const {
    double value = cash_;
    for (const auto& [symbol, quantity] : positions_) {
        const auto price = last_prices_.find(symbol);
        if (price != last_prices_.end()) {
            value += quantity * price->second;
        }
    }
    return value;
}

Quantity Portfolio::position(const Symbol& symbol) const {
    const auto it = positions_.find(symbol);
    return it == positions_.end() ? 0.0 : it->second;
}

std::optional<Price> Portfolio::last_price(const Symbol& symbol) const {
    const auto it = last_prices_.find(symbol);
    if (it == last_prices_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<OrderEvent> Portfolio::on_signal(const SignalEvent& signal) {
    const auto price_it = last_prices_.find(signal.symbol);
    if (price_it == last_prices_.end() || !(price_it->second > 0.0)) {
        // No price yet means no way to size the trade. Skipping is the only
        // honest option; guessing a price here would be inventing data.
        return std::nullopt;
    }
    const Price price = price_it->second;

    const double account = equity();
    if (!(account > 0.0)) {
        return std::nullopt;  // wiped out; stop trading rather than lever up
    }

    const Quantity target_quantity = signal.target_weight * account / price;
    const Quantity delta = target_quantity - position(signal.symbol);

    // Deadband: ignore drift too small to be worth the round trip.
    if (std::abs(delta) * price < config_.min_trade_fraction * account) {
        return std::nullopt;
    }

    return OrderEvent{.ts = signal.ts, .symbol = signal.symbol, .quantity = delta};
}

void Portfolio::on_fill(const FillEvent& fill) {
    positions_[fill.symbol] += fill.quantity;
    // Slippage is already inside fill_price, so only commission is deducted
    // here; subtracting it again would double-count.
    cash_ -= fill.quantity * fill.fill_price + fill.commission;

    total_commission_ += fill.commission;
    total_slippage_ += fill.slippage;
    total_traded_notional_ += std::abs(fill.quantity) * fill.fill_price;

    fills_.push_back(fill);
}

void Portfolio::record_equity(Timestamp ts) {
    const double account = equity();

    double gross = 0.0;
    double net = 0.0;
    for (const auto& [symbol, quantity] : positions_) {
        const auto price = last_prices_.find(symbol);
        if (price == last_prices_.end()) {
            continue;
        }
        const double value = quantity * price->second;
        gross += std::abs(value);
        net += value;

        if (quantity != 0.0) {
            position_history_.push_back(PositionRecord{
                .ts = ts,
                .symbol = symbol,
                .quantity = quantity,
                .weight = account != 0.0 ? value / account : 0.0});
        }
    }

    equity_curve_.push_back(EquityPoint{
        .ts = ts,
        .equity = account,
        .gross_equity = account + total_costs(),
        .gross_exposure = account != 0.0 ? gross / account : 0.0,
        .net_exposure = account != 0.0 ? net / account : 0.0});
}

}  // namespace backtest
