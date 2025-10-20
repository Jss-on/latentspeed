# Phase 2: Order Tracking & State Management

**Duration**: Week 2  
**Priority**: 🔴 Critical  
**Dependencies**: Phase 1 (ConnectorBase)

## Objectives

1. Implement `InFlightOrder` with state machine
2. Create `ClientOrderTracker` for centralized tracking
3. Define `OrderUpdate` and `TradeUpdate` structures
4. Implement event system

---

## 1. InFlightOrder Class

**File**: `include/connector/in_flight_order.h`

```cpp
#pragma once

#include <string>
#include <optional>
#include <vector>
#include <chrono>
#include <future>
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
    
    double fill_price;                 // Execution price
    double fill_base_amount;           // Amount in base currency
    double fill_quote_amount;          // Amount in quote currency
    
    std::string fee_currency;          // Fee currency
    double fee_amount;                 // Fee amount
    
    std::optional<std::string> liquidity;  // "maker" or "taker"
    uint64_t fill_timestamp;           // Timestamp in nanoseconds
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
    uint64_t update_timestamp;
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
    OrderType order_type;
    TradeType trade_type;
    PositionAction position_action;
    
    double price;
    double amount;
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
    
    /**
     * @brief Async wait for exchange order ID
     * @param timeout Maximum time to wait
     * @return Exchange order ID or empty if timeout
     * 
     * This is useful when you need the exchange order ID but it might
     * not be available immediately (e.g., for cancellation).
     */
    std::optional<std::string> get_exchange_order_id_async(
        std::chrono::milliseconds timeout = std::chrono::seconds(5)
    ) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (exchange_order_id.has_value()) {
            return exchange_order_id;
        }
        
        // Wait for exchange_order_id to be set or timeout
        bool success = cv_.wait_for(lock, timeout, [this] {
            return exchange_order_id.has_value() || is_done();
        });
        
        return exchange_order_id;
    }
    
    /**
     * @brief Notify waiters that exchange_order_id is set
     */
    void notify_exchange_order_id_ready() {
        cv_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace latentspeed::connector
```

---

## 2. ClientOrderTracker Class

**File**: `include/connector/client_order_tracker.h`

```cpp
#pragma once

#include "connector/in_flight_order.h"
#include "connector/events.h"
#include <unordered_map>
#include <shared_mutex>
#include <functional>

namespace latentspeed::connector {

/**
 * @class ClientOrderTracker
 * @brief Centralized tracking of all in-flight orders (Hummingbot pattern)
 * 
 * This class maintains the state of all active orders and provides
 * thread-safe access to order information. It processes updates from
 * both REST API responses and WebSocket streams.
 */
class ClientOrderTracker {
public:
    // ========================================================================
    // ORDER LIFECYCLE MANAGEMENT
    // ========================================================================
    
    /**
     * @brief Start tracking an order (MUST be called BEFORE submission)
     */
    void start_tracking(InFlightOrder order) {
        std::unique_lock lock(mutex_);
        
        const std::string& order_id = order.client_order_id;
        tracked_orders_.emplace(order_id, std::move(order));
        
        LOG_DEBUG("Started tracking order: {}", order_id);
    }
    
    /**
     * @brief Stop tracking an order (called when order reaches terminal state)
     */
    void stop_tracking(const std::string& client_order_id) {
        std::unique_lock lock(mutex_);
        
        auto it = tracked_orders_.find(client_order_id);
        if (it != tracked_orders_.end()) {
            LOG_DEBUG("Stopped tracking order: {}", client_order_id);
            tracked_orders_.erase(it);
        }
    }

    // ========================================================================
    // ORDER ACCESS
    // ========================================================================
    
    /**
     * @brief Get order by client order ID (thread-safe)
     */
    std::optional<InFlightOrder> get_order(const std::string& client_order_id) const {
        std::shared_lock lock(mutex_);
        
        auto it = tracked_orders_.find(client_order_id);
        if (it != tracked_orders_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    /**
     * @brief Get order by exchange order ID
     */
    std::optional<InFlightOrder> get_order_by_exchange_id(
        const std::string& exchange_order_id
    ) const {
        std::shared_lock lock(mutex_);
        
        for (const auto& [_, order] : tracked_orders_) {
            if (order.exchange_order_id == exchange_order_id) {
                return order;
            }
        }
        return std::nullopt;
    }
    
    /**
     * @brief Get all fillable orders (OPEN or PARTIALLY_FILLED)
     */
    std::unordered_map<std::string, InFlightOrder> all_fillable_orders() const {
        std::shared_lock lock(mutex_);
        
        std::unordered_map<std::string, InFlightOrder> result;
        for (const auto& [id, order] : tracked_orders_) {
            if (order.is_fillable()) {
                result.emplace(id, order);
            }
        }
        return result;
    }
    
    /**
     * @brief Get fillable orders indexed by exchange order ID
     */
    std::unordered_map<std::string, InFlightOrder> all_fillable_orders_by_exchange_id() const {
        std::shared_lock lock(mutex_);
        
        std::unordered_map<std::string, InFlightOrder> result;
        for (const auto& [_, order] : tracked_orders_) {
            if (order.is_fillable() && order.exchange_order_id.has_value()) {
                result.emplace(*order.exchange_order_id, order);
            }
        }
        return result;
    }
    
    /**
     * @brief Get count of active orders
     */
    size_t active_order_count() const {
        std::shared_lock lock(mutex_);
        return tracked_orders_.size();
    }

    // ========================================================================
    // STATE UPDATE PROCESSING (HUMMINGBOT PATTERN)
    // ========================================================================
    
    /**
     * @brief Process order state update
     * @param update Order update from REST or WebSocket
     */
    void process_order_update(const OrderUpdate& update) {
        std::unique_lock lock(mutex_);
        
        auto it = tracked_orders_.find(update.client_order_id);
        if (it == tracked_orders_.end()) {
            LOG_WARN("Received update for unknown order: {}", update.client_order_id);
            return;
        }
        
        InFlightOrder& order = it->second;
        OrderState old_state = order.current_state;
        
        // Update state
        order.current_state = update.new_state;
        order.last_update_timestamp = update.update_timestamp;
        
        // Update exchange order ID if provided
        if (update.exchange_order_id.has_value()) {
            order.exchange_order_id = update.exchange_order_id;
            order.notify_exchange_order_id_ready();
        }
        
        LOG_INFO("Order {} state: {} -> {}", 
                 update.client_order_id,
                 to_string(old_state),
                 to_string(update.new_state));
        
        // Emit event
        trigger_order_event(OrderEventType::ORDER_UPDATE, update.client_order_id);
        
        // Auto-stop tracking if done
        if (order.is_done() && auto_cleanup_) {
            LOG_DEBUG("Order {} completed, auto-removing from tracker", update.client_order_id);
            tracked_orders_.erase(it);
        }
    }
    
    /**
     * @brief Process trade/fill update
     * @param update Trade update from WebSocket
     */
    void process_trade_update(const TradeUpdate& update) {
        std::unique_lock lock(mutex_);
        
        auto it = tracked_orders_.find(update.client_order_id);
        if (it == tracked_orders_.end()) {
            LOG_WARN("Received trade update for unknown order: {}", update.client_order_id);
            return;
        }
        
        InFlightOrder& order = it->second;
        
        // Add trade to history
        order.trade_fills.push_back(update);
        order.filled_amount += update.fill_base_amount;
        order.last_update_timestamp = update.fill_timestamp;
        
        // Recalculate average fill price
        double total_quote = 0.0;
        double total_base = 0.0;
        for (const auto& fill : order.trade_fills) {
            total_quote += fill.fill_quote_amount;
            total_base += fill.fill_base_amount;
        }
        order.average_fill_price = (total_base > 1e-10) ? (total_quote / total_base) : 0.0;
        
        // Update state based on fill amount
        if (order.filled_amount >= order.amount - 1e-8) {
            order.current_state = OrderState::FILLED;
            LOG_INFO("Order {} fully filled at avg price {}", 
                     update.client_order_id, order.average_fill_price);
        } else {
            order.current_state = OrderState::PARTIALLY_FILLED;
            LOG_INFO("Order {} partially filled: {}/{}", 
                     update.client_order_id, order.filled_amount, order.amount);
        }
        
        // Emit events
        trigger_order_event(OrderEventType::ORDER_FILLED, update.client_order_id);
        
        if (order.is_done()) {
            trigger_order_event(OrderEventType::ORDER_COMPLETED, update.client_order_id);
            
            if (auto_cleanup_) {
                tracked_orders_.erase(it);
            }
        }
    }
    
    /**
     * @brief Process order not found (for DEX order tracking after cancel)
     */
    void process_order_not_found(const std::string& client_order_id) {
        not_found_count_[client_order_id]++;
        
        // After multiple "not found" responses, consider it cancelled
        if (not_found_count_[client_order_id] >= max_not_found_retries_) {
            OrderUpdate update{
                .client_order_id = client_order_id,
                .new_state = OrderState::CANCELLED,
                .update_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            };
            process_order_update(update);
            not_found_count_.erase(client_order_id);
        }
    }

    // ========================================================================
    // EVENT SYSTEM
    // ========================================================================
    
    /**
     * @brief Set event callback
     */
    void set_event_callback(std::function<void(OrderEventType, const std::string&)> callback) {
        event_callback_ = std::move(callback);
    }
    
    /**
     * @brief Enable/disable auto cleanup of completed orders
     */
    void set_auto_cleanup(bool enabled) {
        auto_cleanup_ = enabled;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, InFlightOrder> tracked_orders_;
    std::unordered_map<std::string, int> not_found_count_;
    
    std::function<void(OrderEventType, const std::string&)> event_callback_;
    bool auto_cleanup_ = true;
    int max_not_found_retries_ = 3;
    
    void trigger_order_event(OrderEventType type, const std::string& order_id) {
        if (event_callback_) {
            event_callback_(type, order_id);
        }
    }
};

} // namespace latentspeed::connector
```

---

## 3. Event System

**File**: `include/connector/events.h`

```cpp
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
    ORDER_EXPIRED,           // Order expired
    ORDER_FAILED             // Order failed/rejected
};

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
 */
class OrderEventListener {
public:
    virtual ~OrderEventListener() = default;
    
    virtual void on_order_created(const std::string& client_order_id,
                                   const std::string& exchange_order_id) = 0;
    
    virtual void on_order_filled(const std::string& client_order_id,
                                  double fill_price,
                                  double fill_amount) = 0;
    
    virtual void on_order_completed(const std::string& client_order_id,
                                     double average_fill_price,
                                     double total_filled) = 0;
    
    virtual void on_order_cancelled(const std::string& client_order_id) = 0;
    
    virtual void on_order_failed(const std::string& client_order_id,
                                  const std::string& reason) = 0;
};

/**
 * @class TradeEventListener
 * @brief Interface for receiving trade/fill events
 */
class TradeEventListener {
public:
    virtual ~TradeEventListener() = default;
    
    virtual void on_trade(const std::string& client_order_id,
                          const std::string& trade_id,
                          double price,
                          double amount,
                          const std::string& fee_currency,
                          double fee_amount) = 0;
};

/**
 * @class ErrorEventListener
 * @brief Interface for receiving error events
 */
class ErrorEventListener {
public:
    virtual ~ErrorEventListener() = default;
    
    virtual void on_error(const std::string& error_code,
                          const std::string& error_message) = 0;
};

} // namespace latentspeed::connector
```

---

## 4. Testing Examples

```cpp
// Test order lifecycle
TEST(ClientOrderTracker, OrderLifecycle) {
    ClientOrderTracker tracker;
    
    // Create order
    InFlightOrder order{
        .client_order_id = "test_order_1",
        .trading_pair = "BTC-USD",
        .order_type = OrderType::LIMIT,
        .trade_type = TradeType::BUY,
        .price = 50000.0,
        .amount = 0.1,
        .creation_timestamp = 1234567890
    };
    
    // Start tracking
    tracker.start_tracking(order);
    EXPECT_EQ(tracker.active_order_count(), 1);
    
    // Process order created update
    OrderUpdate created_update{
        .client_order_id = "test_order_1",
        .exchange_order_id = "exchange_123",
        .new_state = OrderState::OPEN,
        .update_timestamp = 1234567891
    };
    tracker.process_order_update(created_update);
    
    auto tracked = tracker.get_order("test_order_1");
    ASSERT_TRUE(tracked.has_value());
    EXPECT_EQ(tracked->current_state, OrderState::OPEN);
    EXPECT_EQ(tracked->exchange_order_id, "exchange_123");
    
    // Process fill
    TradeUpdate fill{
        .trade_id = "trade_1",
        .client_order_id = "test_order_1",
        .fill_price = 50100.0,
        .fill_base_amount = 0.1,
        .fill_quote_amount = 5010.0,
        .fee_currency = "USDT",
        .fee_amount = 5.01,
        .fill_timestamp = 1234567892
    };
    tracker.process_trade_update(fill);
    
    tracked = tracker.get_order("test_order_1");
    ASSERT_TRUE(tracked.has_value());
    EXPECT_EQ(tracked->current_state, OrderState::FILLED);
    EXPECT_DOUBLE_EQ(tracked->filled_amount, 0.1);
    EXPECT_DOUBLE_EQ(tracked->average_fill_price, 50100.0);
    
    // Auto-cleanup enabled, should be removed
    EXPECT_EQ(tracker.active_order_count(), 0);
}
```

---

## Next: Phase 3 - Data Sources

See [04_PHASE3_DATA_SOURCES.md](04_PHASE3_DATA_SOURCES.md) for:
- OrderBookTrackerDataSource
- UserStreamTrackerDataSource
- Exchange-specific implementations
