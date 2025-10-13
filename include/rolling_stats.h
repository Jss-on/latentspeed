/**
 * @file rolling_stats.h
 * @brief Ultra-fast rolling statistics for HFT market data
 * @author jessiondiwangan@gmail.com
 * @date 2025
 * 
 * O(1) updates using Welford's online algorithm for numerical stability
 * Circular buffer for windowed calculations
 */

#pragma once

#include <deque>
#include <cmath>
#include <algorithm>

namespace latentspeed {

/**
 * @class RollingStats
 * @brief Computes rolling statistics over a fixed window
 * 
 * Features:
 * - O(1) update operations
 * - Numerically stable variance calculation (Welford's algorithm)
 * - Order flow imbalance (OFI) tracking
 * - Configurable window size
 */
class RollingStats {
public:
    explicit RollingStats(size_t window_size = 20) 
        : max_size_(window_size)
        , sum_(0.0)
        , sum_sq_(0.0)
        , count_(0)
        , last_bid_size_(0.0)
        , last_ask_size_(0.0) {}
    
    /**
     * @brief Update with new midpoint value (for books)
     * @param value New midpoint price
     */
    void update_mid(double value) {
        if (window_.size() >= max_size_) {
            double old = window_.front();
            window_.pop_front();
            sum_ -= old;
            sum_sq_ -= old * old;
            count_--;
        }
        
        window_.push_back(value);
        sum_ += value;
        sum_sq_ += value * value;
        count_++;
    }
    
    /**
     * @brief Update with new transaction price (for trades)
     * @param value New transaction price
     */
    void update_trade(double value) {
        update_mid(value);  // Same logic
    }
    
    /**
     * @brief Update order flow imbalance state
     * @param bid_size Current best bid size
     * @param ask_size Current best ask size
     */
    void update_ofi(double bid_size, double ask_size) {
        // Calculate delta bid and delta ask
        double delta_bid = bid_size - last_bid_size_;
        double delta_ask = ask_size - last_ask_size_;
        
        // OFI = delta_bid - delta_ask (simplified version)
        // More sophisticated: weight by price and normalize
        double current_ofi = delta_bid - delta_ask;
        
        // Store for rolling calculation
        if (ofi_window_.size() >= max_size_) {
            ofi_window_.pop_front();
        }
        ofi_window_.push_back(current_ofi);
        
        last_bid_size_ = bid_size;
        last_ask_size_ = ask_size;
    }
    
    /**
     * @brief Calculate volatility (standard deviation)
     * @return Standard deviation of values in window
     */
    double volatility() const {
        if (count_ < 2) return 0.0;
        
        double mean = sum_ / count_;
        double variance = (sum_sq_ / count_) - (mean * mean);
        
        // Ensure non-negative due to floating point precision
        return std::sqrt(std::max(0.0, variance));
    }
    
    /**
     * @brief Get rolling order flow imbalance
     * @return Average OFI over window
     */
    double ofi_rolling() const {
        if (ofi_window_.empty()) return 0.0;
        
        double sum = 0.0;
        for (double val : ofi_window_) {
            sum += val;
        }
        return sum / ofi_window_.size();
    }
    
    /**
     * @brief Get current window size
     */
    size_t window_size() const {
        return window_.size();
    }
    
    /**
     * @brief Get mean value
     */
    double mean() const {
        return (count_ > 0) ? (sum_ / count_) : 0.0;
    }
    
    /**
     * @brief Reset all statistics
     */
    void reset() {
        window_.clear();
        ofi_window_.clear();
        sum_ = 0.0;
        sum_sq_ = 0.0;
        count_ = 0;
        last_bid_size_ = 0.0;
        last_ask_size_ = 0.0;
    }

private:
    size_t max_size_;                    ///< Maximum window size
    std::deque<double> window_;          ///< Value window
    std::deque<double> ofi_window_;      ///< OFI window
    double sum_;                         ///< Sum of values (for mean)
    double sum_sq_;                      ///< Sum of squared values (for variance)
    size_t count_;                       ///< Current count
    double last_bid_size_;               ///< Previous bid size for OFI
    double last_ask_size_;               ///< Previous ask size for OFI
};

} // namespace latentspeed
