/**
 * @file user_stream_tracker_data_source.h
 * @brief Abstract data source for user account updates
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

namespace latentspeed::connector {

/**
 * @struct UserStreamMessage
 * @brief Message from user stream
 */
struct UserStreamMessage {
    enum class Type {
        ORDER_UPDATE,       // Order state changed
        TRADE,              // Trade/fill occurred
        BALANCE_UPDATE,     // Account balance changed
        POSITION_UPDATE     // Position changed (derivatives)
    };
    
    Type type;
    uint64_t timestamp = 0;
    nlohmann::json data;  // Exchange-specific format
};

/**
 * @struct BalanceUpdate
 * @brief Account balance update
 */
struct BalanceUpdate {
    std::string asset;
    double available_balance = 0.0;
    double total_balance = 0.0;
    uint64_t timestamp = 0;
};

/**
 * @class UserStreamTrackerDataSource
 * @brief Abstract data source for user account updates
 * 
 * Each exchange implements this to provide user-specific data:
 * - Order updates (created, filled, cancelled)
 * - Fill notifications
 * - Balance changes
 * - Position changes (for derivatives)
 */
class UserStreamTrackerDataSource {
public:
    virtual ~UserStreamTrackerDataSource() = default;

    // ========================================================================
    // LIFECYCLE
    // ========================================================================
    
    /**
     * @brief Initialize the data source (authenticate, setup WebSocket)
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief Start listening for user stream updates
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
    // SUBSCRIPTION MANAGEMENT
    // ========================================================================
    
    /**
     * @brief Subscribe to order updates
     * 
     * This typically happens automatically after authentication,
     * but some exchanges may require explicit subscription.
     */
    virtual void subscribe_to_order_updates() = 0;
    
    /**
     * @brief Subscribe to balance updates
     */
    virtual void subscribe_to_balance_updates() {
        // Default: no-op (some exchanges don't support this)
    }
    
    /**
     * @brief Subscribe to position updates (derivatives)
     */
    virtual void subscribe_to_position_updates() {
        // Default: no-op for spot exchanges
    }

    // ========================================================================
    // MESSAGE CALLBACK (PUSH MODEL)
    // ========================================================================
    
    /**
     * @brief Set callback for received messages
     */
    void set_message_callback(std::function<void(const UserStreamMessage&)> callback) {
        message_callback_ = std::move(callback);
    }

protected:
    std::function<void(const UserStreamMessage&)> message_callback_;
    
    /**
     * @brief Helper to emit messages to callback
     */
    void emit_message(const UserStreamMessage& msg) {
        if (message_callback_) {
            message_callback_(msg);
        }
    }
};

} // namespace latentspeed::connector
