# Phase 1: Core Connector Base Architecture

**Duration**: Week 1  
**Priority**: 🔴 Critical  
**Dependencies**: None

## Objectives

1. Create `ConnectorBase` abstract class
2. Create `PerpetualDerivativeBase` for derivatives
3. Define common interfaces and lifecycle hooks
4. Establish event system foundation

## Component Diagram

```
┌─────────────────────────────────────────┐
│         ConnectorBase                    │
│  (Abstract base for all exchanges)      │
├─────────────────────────────────────────┤
│ + name() -> string                      │
│ + domain() -> string                    │
│ + connector_type() -> ConnectorType     │
│ + initialize() -> bool                  │
│ + connect() -> bool                     │
│ + disconnect()                          │
│ + buy(params) -> string                 │
│ + sell(params) -> string                │
│ + cancel(order_id) -> bool              │
│ + set_order_event_listener()            │
│ # start_tracking_order()                │
│ # _place_order() -> Task<...>           │
│ # _place_order_and_process_update()     │
└─────────────────────────────────────────┘
                   ▲
                   │
        ┌──────────┴──────────┐
        │                     │
┌───────┴──────────┐  ┌──────┴──────────────┐
│  SpotExchangeBase│  │ PerpetualDerivative │
│                  │  │      Base           │
└──────────────────┘  └──────┬──────────────┘
                             │
                  ┌──────────┴──────────┐
                  │                     │
    ┌─────────────┴──────┐  ┌──────────┴────────────┐
    │ Hyperliquid        │  │   DydxV4Perpetual     │
    │ PerpetualConnector │  │   Connector           │
    └────────────────────┘  └───────────────────────┘
```

---

## 1. ConnectorBase Class

**File**: `include/connector/connector_base.h`

```cpp
#pragma once

#include <string>
#include <memory>
#include <optional>
#include <functional>
#include <vector>
#include <coroutine>

#include "connector/in_flight_order.h"
#include "connector/trading_rule.h"
#include "connector/events.h"

namespace latentspeed::connector {

/**
 * @enum ConnectorType
 * @brief Type of exchange connector
 */
enum class ConnectorType {
    SPOT,
    DERIVATIVE_PERPETUAL,
    DERIVATIVE_FUTURES,
    AMM_DEX,
    ORDERBOOK_DEX
};

/**
 * @struct OrderParams
 * @brief Parameters for placing an order
 */
struct OrderParams {
    std::string trading_pair;
    double amount;
    double price;
    OrderType order_type;
    PositionAction position_action = PositionAction::NIL;
    std::optional<int> leverage;
    std::optional<double> trigger_price;
    std::map<std::string, std::string> extra_params;
};

/**
 * @class ConnectorBase
 * @brief Abstract base class for all exchange connectors (Hummingbot pattern)
 * 
 * This class defines the contract that all exchange connectors must implement.
 * It follows Hummingbot's ConnectorBase design with async order placement,
 * order tracking, and event-driven updates.
 */
class ConnectorBase {
public:
    virtual ~ConnectorBase() = default;

    // ========================================================================
    // IDENTITY & LIFECYCLE
    // ========================================================================

    /**
     * @brief Get the connector name (e.g., "hyperliquid_perpetual")
     */
    virtual std::string name() const = 0;

    /**
     * @brief Get the domain (e.g., "hyperliquid_perpetual" or "hyperliquid_perpetual_testnet")
     */
    virtual std::string domain() const = 0;

    /**
     * @brief Get the connector type
     */
    virtual ConnectorType connector_type() const = 0;

    /**
     * @brief Initialize the connector with credentials
     * @return true if initialization successful
     */
    virtual bool initialize() = 0;

    /**
     * @brief Connect to the exchange (WebSocket, gRPC, etc.)
     * @return true if connection successful
     */
    virtual bool connect() = 0;

    /**
     * @brief Disconnect from the exchange
     */
    virtual void disconnect() = 0;

    /**
     * @brief Check if connector is connected
     */
    virtual bool is_connected() const = 0;

    /**
     * @brief Check if trading is ready (connected + authenticated + market data available)
     */
    virtual bool is_ready() const = 0;

    // ========================================================================
    // ORDER PLACEMENT (PUBLIC API - HUMMINGBOT PATTERN)
    // ========================================================================

    /**
     * @brief Place a BUY order (async, non-blocking)
     * @param params Order parameters
     * @return Client order ID (generated immediately)
     * 
     * IMPORTANT: This returns immediately with a client order ID.
     * The actual order placement happens asynchronously.
     * Use event listeners to track order status.
     */
    virtual std::string buy(const OrderParams& params) = 0;

    /**
     * @brief Place a SELL order (async, non-blocking)
     * @param params Order parameters
     * @return Client order ID (generated immediately)
     */
    virtual std::string sell(const OrderParams& params) = 0;

    /**
     * @brief Cancel an order
     * @param client_order_id Client order ID from buy()/sell()
     * @return true if cancel request submitted successfully
     */
    virtual bool cancel(const std::string& client_order_id) = 0;

    // ========================================================================
    // EVENT LISTENERS
    // ========================================================================

    /**
     * @brief Set listener for order events (created, filled, cancelled, etc.)
     */
    virtual void set_order_event_listener(OrderEventListener* listener) = 0;

    /**
     * @brief Set listener for trade/fill events
     */
    virtual void set_trade_event_listener(TradeEventListener* listener) = 0;

    /**
     * @brief Set listener for error events
     */
    virtual void set_error_event_listener(ErrorEventListener* listener) = 0;

    // ========================================================================
    // METADATA & RULES
    // ========================================================================

    /**
     * @brief Get trading rules for a specific trading pair
     */
    virtual std::optional<TradingRule> get_trading_rule(const std::string& trading_pair) const = 0;

    /**
     * @brief Get all trading rules
     */
    virtual std::vector<TradingRule> get_all_trading_rules() const = 0;

    /**
     * @brief Get current timestamp in nanoseconds
     */
    virtual uint64_t current_timestamp_ns() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

protected:
    // ========================================================================
    // INTERNAL ORDER LIFECYCLE (HUMMINGBOT PATTERN)
    // ========================================================================

    /**
     * @brief Start tracking an order (MUST be called BEFORE API submission)
     * 
     * This is a critical Hummingbot pattern: we start tracking orders
     * before they are sent to the exchange to ensure we don't miss
     * any state updates that arrive quickly via WebSocket.
     */
    virtual void start_tracking_order(InFlightOrder& order) = 0;

    /**
     * @brief Place order on exchange (async implementation)
     * @return (exchange_order_id, timestamp)
     * 
     * This is the actual API call to the exchange. It should:
     * 1. Validate parameters
     * 2. Sign the request
     * 3. Submit to exchange
     * 4. Parse response
     * 5. Return exchange order ID
     */
    virtual Task<std::pair<std::string, uint64_t>> _place_order(
        const std::string& order_id,
        const std::string& trading_pair,
        double amount,
        TradeType trade_type,
        OrderType order_type,
        double price,
        PositionAction position_action = PositionAction::NIL
    ) = 0;

    /**
     * @brief Place order and process the update (wrapper around _place_order)
     * 
     * This method:
     * 1. Calls _place_order()
     * 2. Processes the OrderUpdate
     * 3. Emits order created/failed events
     */
    virtual Task<void> _place_order_and_process_update(InFlightOrder& order) = 0;

    /**
     * @brief Cancel order on exchange (async implementation)
     */
    virtual Task<bool> _cancel_order(const std::string& client_order_id) = 0;

    // ========================================================================
    // UTILITY METHODS
    // ========================================================================

    /**
     * @brief Generate a new unique client order ID
     */
    virtual std::string generate_client_order_id() const;

    /**
     * @brief Quantize order price according to trading rules
     */
    virtual double quantize_order_price(const std::string& trading_pair, double price) const;

    /**
     * @brief Quantize order amount according to trading rules
     */
    virtual double quantize_order_amount(const std::string& trading_pair, double amount) const;

protected:
    // Event listeners (stored as weak pointers to avoid circular dependencies)
    OrderEventListener* order_event_listener_ = nullptr;
    TradeEventListener* trade_event_listener_ = nullptr;
    ErrorEventListener* error_event_listener_ = nullptr;

    // Client order ID prefix (e.g., "HB" for Hummingbot)
    std::string client_order_id_prefix_ = "LS";  // LatentSpeed
    
    // Client order ID counter (atomic for thread-safety)
    mutable std::atomic<uint64_t> order_id_counter_{0};
};

// ============================================================================
// COROUTINE SUPPORT (C++20)
// ============================================================================

/**
 * @brief Task type for async operations
 */
template<typename T>
class Task {
public:
    struct promise_type {
        T value;
        std::exception_ptr exception;

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void unhandled_exception() { exception = std::current_exception(); }
        
        template<typename U>
        void return_value(U&& val) { value = std::forward<U>(val); }
    };

    explicit Task(std::coroutine_handle<promise_type> h) : handle_(h) {}
    ~Task() { if (handle_) handle_.destroy(); }

    Task(Task&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    T get() {
        if (handle_.promise().exception) {
            std::rethrow_exception(handle_.promise().exception);
        }
        return handle_.promise().value;
    }

private:
    std::coroutine_handle<promise_type> handle_;
};

} // namespace latentspeed::connector
```

---

## 2. PerpetualDerivativeBase Class

**File**: `include/connector/perpetual_derivative_base.h`

```cpp
#pragma once

#include "connector/connector_base.h"
#include "connector/position.h"
#include <unordered_map>

namespace latentspeed::connector {

/**
 * @class PerpetualDerivativeBase
 * @brief Base class for perpetual derivative exchanges
 * 
 * Adds derivative-specific functionality:
 * - Position management
 * - Leverage control
 * - Funding rate tracking
 * - Mark price / index price
 */
class PerpetualDerivativeBase : public ConnectorBase {
public:
    ConnectorType connector_type() const override {
        return ConnectorType::DERIVATIVE_PERPETUAL;
    }

    // ========================================================================
    // DERIVATIVE-SPECIFIC OPERATIONS
    // ========================================================================

    /**
     * @brief Set leverage for a symbol
     */
    virtual Task<bool> set_leverage(const std::string& symbol, int leverage) = 0;

    /**
     * @brief Get current position for a symbol
     */
    virtual std::optional<Position> get_position(const std::string& symbol) const = 0;

    /**
     * @brief Get all active positions
     */
    virtual std::vector<Position> get_all_positions() const = 0;

    /**
     * @brief Get current funding rate for a symbol
     */
    virtual std::optional<double> get_funding_rate(const std::string& symbol) const = 0;

    /**
     * @brief Get mark price for a symbol
     */
    virtual std::optional<double> get_mark_price(const std::string& symbol) const = 0;

    /**
     * @brief Get index price for a symbol
     */
    virtual std::optional<double> get_index_price(const std::string& symbol) const = 0;

protected:
    // Position tracking
    std::unordered_map<std::string, Position> positions_;
    mutable std::shared_mutex positions_mutex_;

    // Funding rate cache
    std::unordered_map<std::string, double> funding_rates_;
    mutable std::shared_mutex funding_rates_mutex_;

    // Mark/Index price cache
    std::unordered_map<std::string, double> mark_prices_;
    std::unordered_map<std::string, double> index_prices_;
    mutable std::shared_mutex prices_mutex_;

    /**
     * @brief Update position from user stream
     */
    virtual void update_position(const std::string& symbol, const Position& position);

    /**
     * @brief Update funding rate from market data
     */
    virtual void update_funding_rate(const std::string& symbol, double rate);
};

} // namespace latentspeed::connector
```

---

## 3. Supporting Enums and Types

**File**: `include/connector/types.h`

```cpp
#pragma once

#include <string>

namespace latentspeed::connector {

/**
 * @enum OrderType
 */
enum class OrderType {
    LIMIT,
    MARKET,
    LIMIT_MAKER,  // Post-only
    STOP_LIMIT,
    STOP_MARKET
};

/**
 * @enum TradeType
 */
enum class TradeType {
    BUY,
    SELL
};

/**
 * @enum PositionAction
 */
enum class PositionAction {
    NIL,   // Not specified
    OPEN,  // Open new position
    CLOSE  // Close existing position (reduce-only)
};

/**
 * @brief Convert enum to string
 */
inline std::string to_string(OrderType type) {
    switch (type) {
        case OrderType::LIMIT: return "LIMIT";
        case OrderType::MARKET: return "MARKET";
        case OrderType::LIMIT_MAKER: return "LIMIT_MAKER";
        case OrderType::STOP_LIMIT: return "STOP_LIMIT";
        case OrderType::STOP_MARKET: return "STOP_MARKET";
        default: return "UNKNOWN";
    }
}

inline std::string to_string(TradeType type) {
    return type == TradeType::BUY ? "BUY" : "SELL";
}

inline std::string to_string(PositionAction action) {
    switch (action) {
        case PositionAction::NIL: return "NIL";
        case PositionAction::OPEN: return "OPEN";
        case PositionAction::CLOSE: return "CLOSE";
        default: return "UNKNOWN";
    }
}

} // namespace latentspeed::connector
```

---

## 4. Implementation Notes

### Key Design Decisions

1. **Pure Virtual Interface**: All core methods are pure virtual to enforce implementation
2. **Coroutine Support**: Use C++20 coroutines for async operations
3. **Event-Driven**: Listeners instead of callbacks for better decoupling
4. **Thread-Safe**: Use shared_mutex for read-heavy operations
5. **Hummingbot Patterns**: `start_tracking_order()` before API call, non-blocking `buy()/sell()`

### Memory Management

- Use smart pointers (`std::unique_ptr`, `std::shared_ptr`)
- Avoid raw pointers except for observer patterns (listeners)
- Leverage move semantics for large objects

### Error Handling

- Use exceptions for fatal errors
- Use `std::optional` for nullable returns
- Emit error events for non-fatal failures

---

## 5. Usage Example

```cpp
// Create connector
auto connector = std::make_unique<HyperliquidPerpetualConnector>(
    "api_key", "api_secret", false /* use_vault */
);

// Set event listener
connector->set_order_event_listener(&my_listener);

// Initialize and connect
if (!connector->initialize()) {
    throw std::runtime_error("Failed to initialize");
}
if (!connector->connect()) {
    throw std::runtime_error("Failed to connect");
}

// Wait for ready
while (!connector->is_ready()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// Place order (non-blocking!)
OrderParams params{
    .trading_pair = "BTC-USD",
    .amount = 0.1,
    .price = 50000.0,
    .order_type = OrderType::LIMIT
};
std::string client_order_id = connector->buy(params);

// Order events will arrive via listener
// my_listener.on_order_created(client_order_id, exchange_order_id);
// my_listener.on_order_filled(client_order_id, fill_data);
```

---

## 6. Testing Strategy

### Unit Tests

- Test order ID generation
- Test quantization logic
- Test state transitions
- Test event emission

### Integration Tests

- Test with mock exchange API
- Verify order tracking lifecycle
- Test error scenarios
- Test concurrent order placement

### Performance Tests

- Measure order placement latency
- Verify zero allocations in hot path
- Test under high load (1000+ orders/sec)

---

## Next: Phase 2 - Order Tracking

See [03_PHASE2_ORDER_TRACKING.md](03_PHASE2_ORDER_TRACKING.md) for:
- InFlightOrder implementation
- ClientOrderTracker implementation
- OrderUpdate and TradeUpdate structures
