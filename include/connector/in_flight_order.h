/**
 * @file in_flight_order.h
 * @brief Order state machine and tracking (Hummingbot pattern)
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#pragma once

#include <string>
#include <optional>
#include <vector>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include "connector/types.h"

namespace latentspeed::connector {

/**
 * @enum OrderState
 * @brief Order lifecycle states (Hummingbot pattern)
 */
enum class OrderState {
    PENDING_CREATE,      // Created locally, not submitted yet
    PENDING_SUBMIT,      // Submitted to exchange, awaiting response
    OPEN,                // Resting on orderbook
    PARTIALLY_FILLED,    // Some fills received
    FILLED,              // Fully filled
    PENDING_CANCEL,      // Cancel requested
    CANCELLED,           // Confirmed cancelled
    FAILED,              // Rejected by exchange
    EXPIRED              // Expired (e.g., dYdX goodTilBlock reached)
};

/**
 * @brief Convert OrderState to string
 */
inline std::string to_string(OrderState state) {
    switch (state) {
        case OrderState::PENDING_CREATE: return "PENDING_CREATE";
        case OrderState::PENDING_SUBMIT: return "PENDING_SUBMIT";
        case OrderState::OPEN: return "OPEN";
        case OrderState::PARTIALLY_FILLED: return "PARTIALLY_FILLED";
        case OrderState::FILLED: return "FILLED";
        case OrderState::PENDING_CANCEL: return "PENDING_CANCEL";
        case OrderState::CANCELLED: return "CANCELLED";
        case OrderState::FAILED: return "FAILED";
        case OrderState::EXPIRED: return "EXPIRED";
        default: return "UNKNOWN";
    }
}

/**
 * @struct TradeUpdate
 * @brief Represents a fill/trade for an order
 */
struct TradeUpdate {
    std::string trade_id;              // Unique trade ID from exchange
    std::string client_order_id;       // Links to InFlightOrder
    std::string exchange_order_id;     // Exchange order ID
    std::string trading_pair;          // Trading pair
    
    double fill_price = 0.0;           // Execution price
    double fill_base_amount = 0.0;     // Amount in base currency
    double fill_quote_amount = 0.0;    // Amount in quote currency
    
    std::string fee_currency;          // Fee currency
    double fee_amount = 0.0;           // Fee amount
    
    std::optional<std::string> liquidity;  // "maker" or "taker"
    uint64_t fill_timestamp = 0;       // Timestamp in nanoseconds
};

/**
 * @struct OrderUpdate
 * @brief State update for an order
 */
struct OrderUpdate {
    std::string client_order_id;
    std::optional<std::string> exchange_order_id;
    std::string trading_pair;
    OrderState new_state;
    uint64_t update_timestamp = 0;
    std::optional<std::string> reason;  // For failures
};

/**
 * @class InFlightOrder
 * @brief Tracks the state of an active order (Hummingbot pattern)
 * 
 * This class represents an order that is being tracked by the connector.
 * It maintains the full lifecycle state, fill history, and provides
 * async wait capabilities for exchange order ID.
 */
class InFlightOrder {
public:
    // ========================================================================
    // CORE IDENTIFIERS
    // ========================================================================
    
    std::string client_order_id;              // Primary key (set at creation)
    std::optional<std::string> exchange_order_id;  // Set after exchange response

    // ========================================================================
    // ORDER PARAMETERS
    // ========================================================================
    
    std::string trading_pair;
    OrderType order_type = OrderType::LIMIT;
    TradeType trade_type = TradeType::BUY;
    PositionAction position_action = PositionAction::NIL;
    
    double price = 0.0;
    double amount = 0.0;
    std::optional<int> leverage;

    // ========================================================================
    // STATE TRACKING
    // ========================================================================
    
    OrderState current_state = OrderState::PENDING_CREATE;
    
    double filled_amount = 0.0;
    double average_fill_price = 0.0;
    std::vector<TradeUpdate> trade_fills;
    
    uint64_t creation_timestamp = 0;
    uint64_t last_update_timestamp = 0;

    // ========================================================================
    // EXCHANGE-SPECIFIC FIELDS
    // ========================================================================
    
    // Hyperliquid
    std::optional<std::string> cloid;          // 128-bit hex client order ID
    
    // dYdX v4
    std::optional<uint64_t> good_til_block;
    std::optional<uint64_t> good_til_block_time;
    std::optional<int> client_id_numeric;      // Integer client ID for dYdX

    // ========================================================================
    // STATE QUERIES
    // ========================================================================
    
    /**
     * @brief Check if order is in a terminal state
     */
    bool is_done() const {
        return current_state == OrderState::FILLED ||
               current_state == OrderState::CANCELLED ||
               current_state == OrderState::FAILED ||
               current_state == OrderState::EXPIRED;
    }
    
    /**
     * @brief Check if order can receive fills
     */
    bool is_fillable() const {
        return current_state == OrderState::OPEN ||
               current_state == OrderState::PARTIALLY_FILLED;
    }
    
    /**
     * @brief Check if order is active (not done)
     */
    bool is_active() const {
        return !is_done();
    }
    
    /**
     * @brief Get remaining amount to fill
     */
    double remaining_amount() const {
        return amount - filled_amount;
    }

    // ========================================================================
    // ASYNC EXCHANGE ORDER ID RETRIEVAL
    // ========================================================================
    
    // Note: Async wait for exchange_order_id should be handled at the
    // ClientOrderTracker level, not within the order itself, to maintain
    // copyability of InFlightOrder objects
};

} // namespace latentspeed::connector
