# Phase 3: OrderBook & UserStream Data Sources

**Duration**: Week 2  
**Priority**: 🟡 Medium  
**Dependencies**: Phase 1 (ConnectorBase), Phase 2 (Order Tracking)

## Objectives

1. Create abstraction for OrderBook data sources
2. Create abstraction for UserStream data sources
3. Separate market data from user data
4. Enable independent lifecycle management

---

## Architecture Overview

```
┌──────────────────────────────────────────────────┐
│          Connector (e.g., Hyperliquid)           │
├──────────────────────────────────────────────────┤
│                                                   │
│  ┌─────────────────────┐  ┌──────────────────┐  │
│  │ OrderBookTracker    │  │ UserStreamTracker│  │
│  │                     │  │                  │  │
│  │  ┌──────────────┐   │  │  ┌───────────┐  │  │
│  │  │ DataSource   │   │  │  │DataSource │  │  │
│  │  │              │   │  │  │           │  │  │
│  │  │ - Snapshot   │   │  │  │- Orders   │  │  │
│  │  │ - Diffs      │   │  │  │- Fills    │  │  │
│  │  │ - Funding    │   │  │  │- Balances │  │  │
│  │  └──────────────┘   │  │  └───────────┘  │  │
│  └─────────────────────┘  └──────────────────┘  │
│         │                          │             │
│         │ WebSocket                │ WebSocket   │
│         ▼                          ▼             │
│    Market Data                User Data          │
└──────────────────────────────────────────────────┘
```

---

## 1. OrderBookTrackerDataSource

**File**: `include/connector/order_book_tracker_data_source.h`

```cpp
#pragma once

#include <string>
#include <memory>
#include <functional>
#include <coroutine>
#include "connector/order_book.h"

namespace latentspeed::connector {

/**
 * @struct OrderBookMessage
 * @brief Message from orderbook data source
 */
struct OrderBookMessage {
    enum class Type {
        SNAPSHOT,
        DIFF,
        TRADE
    };
    
    Type type;
    std::string trading_pair;
    uint64_t timestamp;
    nlohmann::json data;  // Exchange-specific format
};

/**
 * @struct FundingInfo
 * @brief Funding rate information (for perpetuals)
 */
struct FundingInfo {
    std::string trading_pair;
    double funding_rate;
    double mark_price;
    double index_price;
    uint64_t next_funding_time;
    uint64_t timestamp;
};

/**
 * @class OrderBookTrackerDataSource
 * @brief Abstract data source for orderbook updates
 * 
 * Each exchange implements this to provide orderbook data
 * via their specific API (WebSocket, REST polling, etc.)
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
    virtual Task<bool> initialize() = 0;
    
    /**
     * @brief Start listening for orderbook updates
     */
    virtual Task<void> start() = 0;
    
    /**
     * @brief Stop listening
     */
    virtual void stop() = 0;

    // ========================================================================
    // DATA RETRIEVAL
    // ========================================================================
    
    /**
     * @brief Get full orderbook snapshot
     */
    virtual Task<OrderBook> get_snapshot(const std::string& trading_pair) = 0;
    
    /**
     * @brief Get funding rate info (perpetuals only)
     */
    virtual Task<FundingInfo> get_funding_info(const std::string& trading_pair) {
        // Default: not applicable for spot
        co_return FundingInfo{};
    }

    // ========================================================================
    // STREAMING (PUSH MODEL)
    // ========================================================================
    
    /**
     * @brief Listen for orderbook diff messages
     * 
     * This coroutine continuously yields diff messages.
     * The caller should process them in a loop.
     */
    virtual Task<void> listen_for_order_book_diffs() = 0;
    
    /**
     * @brief Listen for orderbook snapshot messages
     */
    virtual Task<void> listen_for_order_book_snapshots() = 0;
    
    /**
     * @brief Listen for trade messages
     */
    virtual Task<void> listen_for_trades() = 0;

    // ========================================================================
    // MESSAGE QUEUE CALLBACK
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
     * @brief Helper to emit messages
     */
    void emit_message(const OrderBookMessage& msg) {
        if (message_callback_) {
            message_callback_(msg);
        }
    }
};

} // namespace latentspeed::connector
```

---

## 2. UserStreamTrackerDataSource

**File**: `include/connector/user_stream_tracker_data_source.h`

```cpp
#pragma once

#include <string>
#include <memory>
#include <functional>
#include "connector/in_flight_order.h"

namespace latentspeed::connector {

/**
 * @struct UserStreamMessage
 * @brief Message from user stream
 */
struct UserStreamMessage {
    enum class Type {
        ORDER_UPDATE,
        TRADE,
        BALANCE_UPDATE,
        POSITION_UPDATE
    };
    
    Type type;
    uint64_t timestamp;
    nlohmann::json data;  // Exchange-specific format
};

/**
 * @struct BalanceUpdate
 * @brief Account balance update
 */
struct BalanceUpdate {
    std::string asset;
    double available_balance;
    double total_balance;
    uint64_t timestamp;
};

/**
 * @class UserStreamTrackerDataSource
 * @brief Abstract data source for user account updates
 * 
 * Each exchange implements this to provide user-specific data:
 * - Order updates
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
    virtual Task<bool> initialize() = 0;
    
    /**
     * @brief Start listening for user stream updates
     */
    virtual Task<void> start() = 0;
    
    /**
     * @brief Stop listening
     */
    virtual void stop() = 0;

    // ========================================================================
    // SUBSCRIPTION MANAGEMENT
    // ========================================================================
    
    /**
     * @brief Subscribe to order updates
     */
    virtual Task<void> subscribe_to_order_updates() = 0;
    
    /**
     * @brief Subscribe to balance updates
     */
    virtual Task<void> subscribe_to_balance_updates() = 0;
    
    /**
     * @brief Subscribe to position updates (derivatives)
     */
    virtual Task<void> subscribe_to_position_updates() {
        // Default: no-op for spot exchanges
        co_return;
    }

    // ========================================================================
    // STREAMING (PUSH MODEL)
    // ========================================================================
    
    /**
     * @brief Listen for all user stream events
     * 
     * This is the main loop that receives and dispatches all
     * user-specific messages from the exchange.
     */
    virtual Task<void> listen_for_user_stream() = 0;

    // ========================================================================
    // MESSAGE QUEUE CALLBACK
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
     * @brief Helper to emit messages
     */
    void emit_message(const UserStreamMessage& msg) {
        if (message_callback_) {
            message_callback_(msg);
        }
    }
};

} // namespace latentspeed::connector
```

---

## 3. OrderBook Structure

**File**: `include/connector/order_book.h`

```cpp
#pragma once

#include <vector>
#include <map>
#include <string>

namespace latentspeed::connector {

/**
 * @struct OrderBookEntry
 * @brief Single price level in orderbook
 */
struct OrderBookEntry {
    double price;
    double size;
    uint64_t timestamp;
};

/**
 * @class OrderBook
 * @brief In-memory orderbook representation
 */
class OrderBook {
public:
    std::string trading_pair;
    uint64_t timestamp;
    uint64_t sequence;  // Sequence number for tracking updates
    
    // Price-sorted levels
    std::map<double, double, std::greater<double>> bids;  // Descending
    std::map<double, double> asks;                        // Ascending
    
    /**
     * @brief Apply a diff update
     */
    void apply_diff(const nlohmann::json& diff);
    
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
};

} // namespace latentspeed::connector
```

---

## 4. Example: Hyperliquid OrderBook Data Source

**File**: `include/connector/hyperliquid/hyperliquid_order_book_data_source.h`

```cpp
#pragma once

#include "connector/order_book_tracker_data_source.h"
#include <websocketpp/client.hpp>

namespace latentspeed::connector {

class HyperliquidOrderBookDataSource : public OrderBookTrackerDataSource {
public:
    HyperliquidOrderBookDataSource(const std::string& domain);
    
    Task<bool> initialize() override;
    Task<void> start() override;
    void stop() override;
    
    Task<OrderBook> get_snapshot(const std::string& trading_pair) override;
    Task<FundingInfo> get_funding_info(const std::string& trading_pair) override;
    
    Task<void> listen_for_order_book_diffs() override;
    Task<void> listen_for_order_book_snapshots() override;
    Task<void> listen_for_trades() override;

private:
    std::string domain_;
    std::string ws_url_;
    
    // WebSocket client
    using WebSocketClient = websocketpp::client<websocketpp::config::asio_tls_client>;
    std::unique_ptr<WebSocketClient> ws_client_;
    websocketpp::connection_hdl ws_connection_;
    
    // Subscribed trading pairs
    std::vector<std::string> subscribed_pairs_;
    
    // Internal helpers
    void on_ws_message(const std::string& message);
    void subscribe_to_orderbook(const std::string& trading_pair);
    std::string coin_from_trading_pair(const std::string& trading_pair);
};

} // namespace latentspeed::connector
```

---

## 5. Example: Hyperliquid UserStream Data Source

**File**: `include/connector/hyperliquid/hyperliquid_user_stream_data_source.h`

```cpp
#pragma once

#include "connector/user_stream_tracker_data_source.h"
#include "connector/hyperliquid/hyperliquid_auth.h"

namespace latentspeed::connector {

class HyperliquidUserStreamDataSource : public UserStreamTrackerDataSource {
public:
    HyperliquidUserStreamDataSource(
        std::shared_ptr<HyperliquidAuth> auth,
        const std::string& domain
    );
    
    Task<bool> initialize() override;
    Task<void> start() override;
    void stop() override;
    
    Task<void> subscribe_to_order_updates() override;
    Task<void> subscribe_to_balance_updates() override;
    
    Task<void> listen_for_user_stream() override;

private:
    std::shared_ptr<HyperliquidAuth> auth_;
    std::string domain_;
    std::string ws_url_;
    
    // WebSocket client
    std::unique_ptr<WebSocketClient> ws_client_;
    websocketpp::connection_hdl ws_connection_;
    
    // Internal helpers
    void on_ws_message(const std::string& message);
    void process_order_message(const nlohmann::json& data);
    void process_fill_message(const nlohmann::json& data);
};

} // namespace latentspeed::connector
```

---

## 6. Integration with Connector

**In connector implementation**:

```cpp
class HyperliquidPerpetualConnector : public PerpetualDerivativeBase {
public:
    HyperliquidPerpetualConnector(...) {
        // Create data sources
        orderbook_data_source_ = std::make_unique<HyperliquidOrderBookDataSource>(domain_);
        user_stream_data_source_ = std::make_unique<HyperliquidUserStreamDataSource>(auth_, domain_);
        
        // Set callbacks
        orderbook_data_source_->set_message_callback([this](const auto& msg) {
            on_orderbook_message(msg);
        });
        
        user_stream_data_source_->set_message_callback([this](const auto& msg) {
            on_user_stream_message(msg);
        });
    }
    
    bool connect() override {
        // Start data sources
        co_await orderbook_data_source_->initialize();
        co_await user_stream_data_source_->initialize();
        
        co_await orderbook_data_source_->start();
        co_await user_stream_data_source_->start();
        
        return true;
    }
    
private:
    std::unique_ptr<OrderBookTrackerDataSource> orderbook_data_source_;
    std::unique_ptr<UserStreamTrackerDataSource> user_stream_data_source_;
    
    void on_orderbook_message(const OrderBookMessage& msg) {
        // Update internal orderbook cache
        // Emit price update events
    }
    
    void on_user_stream_message(const UserStreamMessage& msg) {
        switch (msg.type) {
            case UserStreamMessage::Type::ORDER_UPDATE:
                process_order_update_from_ws(msg.data);
                break;
            case UserStreamMessage::Type::TRADE:
                process_trade_from_ws(msg.data);
                break;
            // ...
        }
    }
};
```

---

## Benefits of This Design

1. **Separation of Concerns**: Market data and user data are independent
2. **Testability**: Can mock data sources for unit testing
3. **Reusability**: Data sources can be shared across connectors
4. **Flexibility**: Easy to swap between WebSocket/REST/gRPC
5. **Type Safety**: Strong typing for message types

---

## Next: Phase 4 - Auth Modules

See [05_PHASE4_AUTH_MODULES.md](05_PHASE4_AUTH_MODULES.md) for:
- Hyperliquid EIP-712 authentication
- dYdX v4 Cosmos SDK authentication
- Signature generation and request signing
