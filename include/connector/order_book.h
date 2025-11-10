/**
 * @file order_book.h
 * @brief OrderBook representation and management
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#pragma once

#include <map>
#include <string>
#include <optional>
#include <vector>
#include <cstdint>
#include <chrono>

namespace latentspeed::connector {

/**
 * @struct OrderBookEntry
 * @brief Single price level in orderbook
 */
struct OrderBookEntry {
    double price = 0.0;
    double size = 0.0;
    uint64_t timestamp = 0;
};

/**
 * @class OrderBook
 * @brief In-memory orderbook representation
 * 
 * Maintains sorted bid/ask levels with best bid/ask at the front.
 * Thread-safety must be handled by the caller.
 */
class OrderBook {
public:
    std::string trading_pair;
    uint64_t timestamp = 0;
    uint64_t sequence = 0;  // Sequence number for tracking updates
    
    // Price-sorted levels
    // Bids: descending (highest price first)
    // Asks: ascending (lowest price first)
    std::map<double, double, std::greater<double>> bids;  // price -> size
    std::map<double, double> asks;                        // price -> size
    
    /**
     * @brief Apply a snapshot update (full orderbook)
     */
    void apply_snapshot(const std::map<double, double>& bid_levels,
                       const std::map<double, double>& ask_levels,
                       uint64_t seq = 0) {
        bids.clear();
        asks.clear();
        
        for (const auto& [price, size] : bid_levels) {
            if (size > 0.0) {
                bids[price] = size;
            }
        }
        
        for (const auto& [price, size] : ask_levels) {
            if (size > 0.0) {
                asks[price] = size;
            }
        }
        
        sequence = seq;
        timestamp = current_timestamp_ns();
    }
    
    /**
     * @brief Apply a differential update
     * @param price Price level to update
     * @param size New size (0 = remove level)
     * @param is_bid True for bid, false for ask
     */
    void apply_delta(double price, double size, bool is_bid) {
        if (is_bid) {
            if (size > 0.0) {
                bids[price] = size;
            } else {
                bids.erase(price);
            }
        } else {
            if (size > 0.0) {
                asks[price] = size;
            } else {
                asks.erase(price);
            }
        }
        
        timestamp = current_timestamp_ns();
    }
    
    /**
     * @brief Get best bid price
     */
    std::optional<double> best_bid() const {
        if (bids.empty()) return std::nullopt;
        return bids.begin()->first;
    }
    
    /**
     * @brief Get best ask price
     */
    std::optional<double> best_ask() const {
        if (asks.empty()) return std::nullopt;
        return asks.begin()->first;
    }
    
    /**
     * @brief Get best bid size
     */
    std::optional<double> best_bid_size() const {
        if (bids.empty()) return std::nullopt;
        return bids.begin()->second;
    }
    
    /**
     * @brief Get best ask size
     */
    std::optional<double> best_ask_size() const {
        if (asks.empty()) return std::nullopt;
        return asks.begin()->second;
    }
    
    /**
     * @brief Get mid price
     */
    std::optional<double> mid_price() const {
        auto bid = best_bid();
        auto ask = best_ask();
        if (!bid.has_value() || !ask.has_value()) return std::nullopt;
        return (*bid + *ask) / 2.0;
    }
    
    /**
     * @brief Get spread
     */
    std::optional<double> spread() const {
        auto bid = best_bid();
        auto ask = best_ask();
        if (!bid.has_value() || !ask.has_value()) return std::nullopt;
        return *ask - *bid;
    }
    
    /**
     * @brief Get spread in basis points (bps)
     */
    std::optional<double> spread_bps() const {
        auto bid = best_bid();
        auto ask = best_ask();
        if (!bid.has_value() || !ask.has_value()) return std::nullopt;
        if (*bid <= 0.0) return std::nullopt;
        return ((*ask - *bid) / *bid) * 10000.0;
    }
    
    /**
     * @brief Get top N levels for display
     */
    std::vector<OrderBookEntry> get_top_bids(size_t n = 10) const {
        std::vector<OrderBookEntry> result;
        result.reserve(std::min(n, bids.size()));
        
        size_t count = 0;
        for (const auto& [price, size] : bids) {
            if (count++ >= n) break;
            result.push_back({price, size, timestamp});
        }
        return result;
    }
    
    /**
     * @brief Get top N ask levels
     */
    std::vector<OrderBookEntry> get_top_asks(size_t n = 10) const {
        std::vector<OrderBookEntry> result;
        result.reserve(std::min(n, asks.size()));
        
        size_t count = 0;
        for (const auto& [price, size] : asks) {
            if (count++ >= n) break;
            result.push_back({price, size, timestamp});
        }
        return result;
    }
    
    /**
     * @brief Check if orderbook is valid (has both bids and asks)
     */
    bool is_valid() const {
        return !bids.empty() && !asks.empty();
    }
    
    /**
     * @brief Clear the orderbook
     */
    void clear() {
        bids.clear();
        asks.clear();
        sequence = 0;
        timestamp = 0;
    }

private:
    static uint64_t current_timestamp_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
};

} // namespace latentspeed::connector
