/**
 * @file events.h
 * @brief Event system for connector framework (Hummingbot pattern)
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#pragma once

#include <string>
#include <functional>

namespace latentspeed::connector {

/**
 * @enum OrderEventType
 * @brief Types of order events (Hummingbot pattern)
 */
enum class OrderEventType {
    ORDER_CREATED,           // Order successfully submitted to exchange
    ORDER_UPDATE,            // Order state changed
    ORDER_FILLED,            // Order received a fill
    ORDER_PARTIALLY_FILLED,  // Order partially filled
    ORDER_COMPLETED,         // Order fully filled
    ORDER_CANCELLED,         // Order cancelled
    ORDER_EXPIRED,           // Order expired (e.g., dYdX goodTilBlock)
    ORDER_FAILED             // Order failed/rejected
};

/**
 * @brief Convert OrderEventType to string
 */
inline std::string to_string(OrderEventType type) {
    switch (type) {
        case OrderEventType::ORDER_CREATED: return "ORDER_CREATED";
        case OrderEventType::ORDER_UPDATE: return "ORDER_UPDATE";
        case OrderEventType::ORDER_FILLED: return "ORDER_FILLED";
        case OrderEventType::ORDER_PARTIALLY_FILLED: return "ORDER_PARTIALLY_FILLED";
        case OrderEventType::ORDER_COMPLETED: return "ORDER_COMPLETED";
        case OrderEventType::ORDER_CANCELLED: return "ORDER_CANCELLED";
        case OrderEventType::ORDER_EXPIRED: return "ORDER_EXPIRED";
        case OrderEventType::ORDER_FAILED: return "ORDER_FAILED";
        default: return "UNKNOWN";
    }
}

/**
 * @class OrderEventListener
 * @brief Interface for receiving order events
 * 
 * Implement this interface to receive notifications about order
 * state changes, fills, and errors.
 */
class OrderEventListener {
public:
    virtual ~OrderEventListener() = default;
    
    /**
     * @brief Called when an order is successfully created on the exchange
     * @param client_order_id Client order ID
     * @param exchange_order_id Exchange-assigned order ID
     */
    virtual void on_order_created(
        const std::string& client_order_id,
        const std::string& exchange_order_id
    ) = 0;
    
    /**
     * @brief Called when an order receives a fill
     * @param client_order_id Client order ID
     * @param fill_price Execution price
     * @param fill_amount Filled quantity
     */
    virtual void on_order_filled(
        const std::string& client_order_id,
        double fill_price,
        double fill_amount
    ) = 0;
    
    /**
     * @brief Called when an order is fully filled
     * @param client_order_id Client order ID
     * @param average_fill_price Average execution price
     * @param total_filled Total filled quantity
     */
    virtual void on_order_completed(
        const std::string& client_order_id,
        double average_fill_price,
        double total_filled
    ) = 0;
    
    /**
     * @brief Called when an order is cancelled
     * @param client_order_id Client order ID
     */
    virtual void on_order_cancelled(
        const std::string& client_order_id
    ) = 0;
    
    /**
     * @brief Called when an order fails or is rejected
     * @param client_order_id Client order ID
     * @param reason Failure reason
     */
    virtual void on_order_failed(
        const std::string& client_order_id,
        const std::string& reason
    ) = 0;
    
    /**
     * @brief Called when an order expires (e.g., dYdX goodTilBlock)
     * @param client_order_id Client order ID
     */
    virtual void on_order_expired(
        const std::string& client_order_id
    ) {
        // Default implementation: treat as cancelled
        on_order_cancelled(client_order_id);
    }
};

/**
 * @class TradeEventListener
 * @brief Interface for receiving trade/fill events
 * 
 * More detailed fill information than OrderEventListener.
 */
class TradeEventListener {
public:
    virtual ~TradeEventListener() = default;
    
    /**
     * @brief Called when a trade/fill occurs
     * @param client_order_id Client order ID
     * @param trade_id Unique trade ID from exchange
     * @param price Execution price
     * @param amount Filled quantity
     * @param fee_currency Fee currency
     * @param fee_amount Fee amount
     */
    virtual void on_trade(
        const std::string& client_order_id,
        const std::string& trade_id,
        double price,
        double amount,
        const std::string& fee_currency,
        double fee_amount
    ) = 0;
};

/**
 * @class ErrorEventListener
 * @brief Interface for receiving error events
 */
class ErrorEventListener {
public:
    virtual ~ErrorEventListener() = default;
    
    /**
     * @brief Called when an error occurs
     * @param error_code Error code
     * @param error_message Human-readable error message
     */
    virtual void on_error(
        const std::string& error_code,
        const std::string& error_message
    ) = 0;
};

/**
 * @class BalanceEventListener
 * @brief Interface for receiving balance update events
 */
class BalanceEventListener {
public:
    virtual ~BalanceEventListener() = default;
    
    /**
     * @brief Called when account balance changes
     * @param asset Asset symbol
     * @param available_balance Available balance
     * @param total_balance Total balance
     */
    virtual void on_balance_update(
        const std::string& asset,
        double available_balance,
        double total_balance
    ) = 0;
};

/**
 * @class PositionEventListener
 * @brief Interface for receiving position update events (derivatives)
 */
class PositionEventListener {
public:
    virtual ~PositionEventListener() = default;
    
    /**
     * @brief Called when position changes
     * @param symbol Trading symbol
     * @param side Position side (LONG/SHORT)
     * @param size Position size
     * @param entry_price Average entry price
     * @param unrealized_pnl Unrealized P&L
     */
    virtual void on_position_update(
        const std::string& symbol,
        const std::string& side,
        double size,
        double entry_price,
        double unrealized_pnl
    ) = 0;
};

} // namespace latentspeed::connector
