#pragma once

#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace backtest {

// Online mean and variance (Welford 1962).
//
// The naive route -- accumulate sum(x) and sum(x^2), then subtract -- loses
// catastrophic precision when the mean is large relative to the spread,
// because it differences two nearly equal large numbers. Welford never forms
// that difference: it tracks the mean and the sum of squared deviations from
// the running mean, both of which stay on the scale of the data.
class WelfordAccumulator {
public:
    void update(double x) noexcept {
        ++count_;
        const double delta = x - mean_;
        mean_ += delta / static_cast<double>(count_);
        // Deliberately (x - mean_) with the *updated* mean; using the old mean
        // here is the classic way to get this subtly wrong.
        m2_ += delta * (x - mean_);
    }

    std::size_t count() const noexcept { return count_; }
    double mean() const noexcept { return mean_; }

    // Sample variance (Bessel-corrected). Undefined for fewer than two
    // observations, and reported as NaN rather than 0 so a caller acting on
    // an unwarmed estimator produces obvious nonsense instead of quiet zeros.
    double variance() const noexcept {
        if (count_ < 2) {
            return std::nan("");
        }
        return m2_ / static_cast<double>(count_ - 1);
    }

    double stddev() const noexcept { return std::sqrt(variance()); }

    void reset() noexcept {
        count_ = 0;
        mean_ = 0.0;
        m2_ = 0.0;
    }

private:
    std::size_t count_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;
};

// Exponentially weighted volatility of returns, zero-mean form:
//
//     var_t = lambda * var_{t-1} + (1 - lambda) * r_t^2
//     lambda = exp(-ln 2 / halflife)
//
// Zero-mean is standard at daily horizon: the mean daily return is negligible
// against its standard deviation, and estimating it adds more noise than it
// removes bias.
//
// var_0 is seeded from the first `seed_count` observations rather than from
// zero, because seeding at zero biases the estimate downward for several
// halflives -- which would inflate positions exactly when vol is unknown.
class EwmaVolatility {
public:
    explicit EwmaVolatility(double halflife, std::size_t seed_count = 20)
        : lambda_(std::exp(-std::numbers::ln2 / halflife)), seed_count_(seed_count) {
        if (halflife <= 0.0) {
            throw std::invalid_argument("EwmaVolatility: halflife must be positive");
        }
        if (seed_count == 0) {
            throw std::invalid_argument("EwmaVolatility: seed_count must be positive");
        }
    }

    void update(double ret) noexcept {
        if (!warmed_up_) {
            seed_.update(ret * ret);
            if (seed_.count() >= seed_count_) {
                variance_ = seed_.mean();
                warmed_up_ = true;
            }
            return;
        }
        variance_ = lambda_ * variance_ + (1.0 - lambda_) * ret * ret;
    }

    // A strategy must not size a position off an unwarmed estimator.
    bool is_warmed_up() const noexcept { return warmed_up_; }

    double variance() const noexcept { return warmed_up_ ? variance_ : std::nan(""); }
    double value() const noexcept { return std::sqrt(variance()); }

    // Per-period sigma scaled to annual, given periods per year.
    double annualised(double periods_per_year = 252.0) const noexcept {
        return value() * std::sqrt(periods_per_year);
    }

    double lambda() const noexcept { return lambda_; }
    std::size_t observations() const noexcept { return seed_.count(); }

private:
    double lambda_;
    std::size_t seed_count_;
    bool warmed_up_ = false;
    double variance_ = 0.0;
    WelfordAccumulator seed_;  // mean of r^2 over the seeding window
};

// Fixed-capacity ring buffer. O(1) push, indexed from the back so that
// `back(0)` is the newest element -- the natural way to express "the close
// 252 bars ago" without arithmetic on a head index at every call site.
template <typename T>
class RollingWindow {
public:
    explicit RollingWindow(std::size_t capacity) : buffer_(capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("RollingWindow: capacity must be positive");
        }
    }

    void push(const T& value) {
        buffer_[head_] = value;
        head_ = (head_ + 1) % buffer_.size();
        if (size_ < buffer_.size()) {
            ++size_;
        }
    }

    // back(0) is the most recent element, back(1) the one before it.
    const T& back(std::size_t offset_from_newest) const {
        if (offset_from_newest >= size_) {
            throw std::out_of_range("RollingWindow::back: offset beyond available history");
        }
        const std::size_t capacity = buffer_.size();
        // head_ points one past the newest, so step back offset+1 slots.
        const std::size_t index = (head_ + capacity - 1 - offset_from_newest) % capacity;
        return buffer_[index];
    }

    const T& oldest() const { return back(size_ - 1); }
    const T& newest() const { return back(0); }

    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return buffer_.size(); }
    bool full() const noexcept { return size_ == buffer_.size(); }
    bool empty() const noexcept { return size_ == 0; }

    void clear() noexcept {
        head_ = 0;
        size_ = 0;
    }

private:
    std::vector<T> buffer_;
    std::size_t head_ = 0;  // next write position
    std::size_t size_ = 0;
};

}  // namespace backtest
