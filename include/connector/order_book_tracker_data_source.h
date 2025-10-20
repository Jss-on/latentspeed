/**
 * @file order_book_tracker_data_source.h
 * @brief Abstract data source for orderbook updates
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <optional>
#include "connector/order_book.h"
#include <nlohmann/json.hpp>

namespace latentspeed::connector {

/**
 * @struct OrderBookMessage
 * @brief Message from orderbook data source
 */
struct OrderBookMessage {
    enum class Type {
        SNAPSHOT,    // Full orderbook snapshot
        DIFF,        // Incremental update
        TRADE        // Trade tick
    };
    
    Type type;
    std::string trading_pair;
    uint64_t timestamp = 0;
    nlohmann::json data;  // Exchange-specific format
};

/**
 * @struct FundingInfo
 * @brief Funding rate information (for perpetuals)
 */
struct FundingInfo {
    std::string trading_pair;
    double funding_rate = 0.0;
    double mark_price = 0.0;
    double index_price = 0.0;
    uint64_t next_funding_time = 0;
    uint64_t timestamp = 0;
};

/**
 * @class OrderBookTrackerDataSource
 * @brief Abstract data source for orderbook updates
 * 
 * Each exchange implements this to provide orderbook data
 * via their specific API (WebSocket, REST polling, gRPC, etc.)
 */
class OrderBookTrackerDataSource {
public:
    virtual ~OrderBookTrackerDataSource() = default;

    // ========================================================================
    // LIFECYCLE
    // ========================================================================
    
    /**
     * @brief Initialize the data source
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief Start listening for orderbook updates
     */
    virtual void start() = 0;
    
    /**
     * @brief Stop listening
     */
    virtual void stop() = 0;
    
    /**
     * @brief Check if connected
     */
    virtual bool is_connected() const = 0;

    // ========================================================================
    // DATA RETRIEVAL (PULL MODEL)
    // ========================================================================
    
    /**
     * @brief Get full orderbook snapshot via REST
     * @param trading_pair Trading pair
     * @return OrderBook or nullopt if unavailable
     */
    virtual std::optional<OrderBook> get_snapshot(const std::string& trading_pair) = 0;
    
    /**
     * @brief Get funding rate info (perpetuals only)
     * @param trading_pair Trading pair
     * @return FundingInfo or nullopt if not applicable
     */
    virtual std::optional<FundingInfo> get_funding_info(const std::string& trading_pair) {
        // Default: not applicable for spot exchanges
        return std::nullopt;
    }

    // ========================================================================
    // SUBSCRIPTION MANAGEMENT
    // ========================================================================
    
    /**
     * @brief Subscribe to orderbook updates for a trading pair
     * @param trading_pair Trading pair to subscribe
     */
    virtual void subscribe_orderbook(const std::string& trading_pair) = 0;
    
    /**
     * @brief Unsubscribe from orderbook updates
     * @param trading_pair Trading pair to unsubscribe
     */
    virtual void unsubscribe_orderbook(const std::string& trading_pair) = 0;

    // ========================================================================
    // MESSAGE CALLBACK (PUSH MODEL)
    // ========================================================================
    
    /**
     * @brief Set callback for received messages
     * 
     * Data source implementations call this to deliver messages
     * to the OrderBookTracker.
     */
    void set_message_callback(std::function<void(const OrderBookMessage&)> callback) {
        message_callback_ = std::move(callback);
    }

protected:
    std::function<void(const OrderBookMessage&)> message_callback_;
    
    /**
     * @brief Helper to emit messages to callback
     */
    void emit_message(const OrderBookMessage& msg) {
        if (message_callback_) {
            message_callback_(msg);
        }
    }
};

} // namespace latentspeed::connector
