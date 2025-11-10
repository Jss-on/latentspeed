# Hyperliquid Connector Integration Plan

**Status**: 📋 Ready for Implementation  
**Date**: 2025-01-27  
**Owner**: @jessiondiwangan

---

## Executive Summary

This document provides a comprehensive plan to integrate the newly refactored **Hummingbot-inspired Hyperliquid connector** into the existing **trading engine service** without disrupting current functionality.

## 1. Architecture Analysis

### 1.1 Current State

The trading engine currently has **TWO parallel systems**:

#### **System A: Phase 1 Adapters (Production)**
```
TradingEngineService
    └── VenueRouter
        ├── BybitAdapter (IExchangeAdapter)
        ├── BinanceAdapter (IExchangeAdapter)
        └── HyperliquidAdapter (IExchangeAdapter) ← SKELETON ONLY
```

**Characteristics:**
- Interface: `IExchangeAdapter` 
- Located in: `include/adapters/` and `src/adapters/`
- Design: Simple wrapper around existing clients
- Status: **In production use** for Bybit/Binance
- Hyperliquid: **Skeleton only** (lines 622-640 in trading_engine_service.cpp)

#### **System B: Phase 3 Connectors (Just Built)**
```
HyperliquidPerpetualConnector (ConnectorBase)
    ├── HyperliquidOrderBookDataSource
    ├── HyperliquidUserStreamDataSource
    ├── HyperliquidAuth
    ├── ClientOrderTracker
    └── InFlightOrder state machine
```

**Characteristics:**
- Interface: `ConnectorBase` (Hummingbot pattern)
- Located in: `include/connector/exchange/hyperliquid/` and `src/connector/exchange/hyperliquid/`
- Design: Full-featured connector with order tracking, lifecycle, data sources
- Status: **Refactored but not integrated**

### 1.2 The Integration Challenge

**Problem**: Two different interfaces that serve the same purpose
- `IExchangeAdapter` (old system) ≠ `ConnectorBase` (new system)
- Trading engine expects `IExchangeAdapter`
- We built `ConnectorBase` implementation

**Solution**: Create a **Bridge Adapter** that wraps `ConnectorBase` to implement `IExchangeAdapter`

---

## 2. Integration Strategy

### Option A: Bridge Adapter Pattern ⭐ **RECOMMENDED**

Create `HyperliquidConnectorAdapter` that bridges the two systems:

```cpp
class HyperliquidConnectorAdapter : public IExchangeAdapter {
private:
    std::shared_ptr<HyperliquidPerpetualConnector> connector_;
    
public:
    // IExchangeAdapter interface → delegate to ConnectorBase
    bool initialize(const std::string& api_key,
                    const std::string& api_secret,
                    bool testnet) override {
        connector_ = std::make_shared<HyperliquidPerpetualConnector>(...);
        return connector_->initialize();
    }
    
    OrderResponse place_order(const OrderRequest& request) override {
        // Translate OrderRequest → OrderParams
        // Call connector_->buy() or connector_->sell()
        // Translate result → OrderResponse
    }
    
    // ... other methods
};
```

**Pros:**
- ✅ No changes to trading engine
- ✅ Preserves all connector functionality
- ✅ Can switch between old and new at runtime
- ✅ Low risk, incremental rollout

**Cons:**
- ❌ Extra layer of translation
- ❌ Slight overhead (negligible for async ops)

### Option B: Direct Migration

Replace `IExchangeAdapter` with `ConnectorBase` throughout:

**Pros:**
- ✅ Clean architecture
- ✅ No adapter overhead

**Cons:**
- ❌ High risk, requires engine refactor
- ❌ Breaks existing Bybit/Binance adapters
- ❌ All-or-nothing approach

**Decision**: **Use Option A (Bridge Adapter)** for safety and flexibility.

---

## 3. Implementation Plan

### Phase 1: Create Bridge Adapter (Week 1)

**Goal**: Wrap `HyperliquidPerpetualConnector` in `IExchangeAdapter` interface

#### 1.1 Create Bridge Adapter Class

**File**: `include/adapters/hyperliquid/hyperliquid_connector_adapter.h`

```cpp
#pragma once

#include "adapters/exchange_adapter.h"
#include "connector/exchange/hyperliquid/hyperliquid_perpetual_connector.h"
#include <memory>
#include <mutex>
#include <unordered_map>

namespace latentspeed {

/**
 * @brief Bridge adapter that wraps HyperliquidPerpetualConnector (Phase 3) 
 *        to implement IExchangeAdapter (Phase 1) interface
 */
class HyperliquidConnectorAdapter final : public IExchangeAdapter {
public:
    HyperliquidConnectorAdapter();
    ~HyperliquidConnectorAdapter() override;

    // IExchangeAdapter interface implementation
    bool initialize(const std::string& api_key,
                    const std::string& api_secret,
                    bool testnet) override;
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;

    OrderResponse place_order(const OrderRequest& request) override;
    OrderResponse cancel_order(const std::string& client_order_id,
                               const std::optional<std::string>& symbol,
                               const std::optional<std::string>& exchange_order_id) override;
    OrderResponse modify_order(const std::string& client_order_id,
                               const std::optional<std::string>& new_quantity,
                               const std::optional<std::string>& new_price) override;
    OrderResponse query_order(const std::string& client_order_id) override;

    void set_order_update_callback(OrderUpdateCallback cb) override;
    void set_fill_callback(FillCallback cb) override;
    void set_error_callback(ErrorCallback cb) override;

    std::string get_exchange_name() const override { return "hyperliquid"; }

    std::vector<OpenOrderBrief> list_open_orders(
        const std::optional<std::string>& category,
        const std::optional<std::string>& symbol,
        const std::optional<std::string>& settle_coin,
        const std::optional<std::string>& base_coin) override;

private:
    // Translation methods
    connector::OrderParams translate_to_order_params(const OrderRequest& request);
    OrderResponse translate_from_connector(const std::string& client_order_id, bool success);
    
    // Event forwarding
    void forward_connector_events();
    
    // State
    std::shared_ptr<connector::HyperliquidPerpetualConnector> connector_;
    OrderUpdateCallback order_update_cb_;
    FillCallback fill_cb_;
    ErrorCallback error_cb_;
    
    std::mutex callbacks_mutex_;
};

} // namespace latentspeed
```

#### 1.2 Implement Translation Logic

**File**: `src/adapters/hyperliquid/hyperliquid_connector_adapter.cpp`

**Key translations:**
1. `OrderRequest` → `connector::OrderParams`
2. Connector events → Adapter callbacks
3. Symbol formats (e.g., "BTCUSDT" → "BTC-USD")
4. Order types and time-in-force mapping

#### 1.3 Update CMakeLists.txt

Add new files to build system:
```cmake
# src/adapters/CMakeLists.txt
add_library(adapters
    # ... existing files ...
    hyperliquid/hyperliquid_connector_adapter.cpp
)

target_link_libraries(adapters
    PUBLIC
        connector  # Link to the new connector library
    # ... other dependencies ...
)
```

### Phase 2: Wire Into Trading Engine (Week 1-2)

#### 2.1 Replace Skeleton Adapter

**File**: `src/trading_engine_service.cpp` (lines 622-640)

**Before:**
```cpp
if (config_.exchange == "hyperliquid") {
    // ... validation ...
    auto adapter = std::make_unique<HyperliquidAdapter>();  // SKELETON
    if (!adapter->initialize(api_key, api_secret, use_testnet)) {
        // ...
    }
    // ...
}
```

**After:**
```cpp
if (config_.exchange == "hyperliquid") {
    // ... validation ...
    auto adapter = std::make_unique<HyperliquidConnectorAdapter>();  // BRIDGE
    if (!adapter->initialize(api_key, api_secret, use_testnet)) {
        spdlog::error("[HFT-Engine] Failed to initialize Hyperliquid connector");
        return false;
    }
    adapter->set_order_update_callback([this](const OrderUpdate& u) { 
        this->on_order_update_hft(u); 
    });
    adapter->set_fill_callback([this](const FillData& f) { 
        this->on_fill_hft(f); 
    });
    if (!adapter->connect()) {
        spdlog::warn("[HFT-Engine] Hyperliquid connector not connected; using HTTP fallback");
    }
    venue_router_->register_adapter(std::move(adapter));
    spdlog::info("[HFT-Engine] Exchange adapter initialized: hyperliquid (full connector)");
    wired = true;
}
```

#### 2.2 Update Include Path

**File**: `src/trading_engine_service.cpp` (line 25)

**Before:**
```cpp
#include "adapters/hyperliquid_adapter.h"
```

**After:**
```cpp
#include "adapters/hyperliquid/hyperliquid_connector_adapter.h"
```

### Phase 3: Testing & Validation (Week 2)

#### 3.1 Unit Tests

**File**: `tests/unit/adapters/test_hyperliquid_connector_adapter.cpp`

Test cases:
- ✅ Adapter initialization with valid credentials
- ✅ OrderRequest → OrderParams translation
- ✅ Connector events → Adapter callbacks
- ✅ Symbol format conversion
- ✅ Order lifecycle (place → update → fill → complete)
- ✅ Error handling and propagation

#### 3.2 Integration Tests

**File**: `tests/integration/test_hyperliquid_engine_integration.cpp`

Test scenarios:
- ✅ Engine startup with Hyperliquid adapter
- ✅ Place market order via ZMQ → receive reports
- ✅ Place limit order → cancel
- ✅ WebSocket updates flow through correctly
- ✅ Open order rehydration on reconnect

#### 3.3 Manual Testing Checklist

- [ ] Start engine with `--exchange hyperliquid`
- [ ] Verify WebSocket connection established
- [ ] Send ExecutionOrder via ZMQ
- [ ] Confirm ExecutionReport published
- [ ] Verify fill events received
- [ ] Test order cancellation
- [ ] Test reconnection logic
- [ ] Monitor memory usage (no leaks)

### Phase 4: Rollout & Monitoring (Week 3)

#### 4.1 Feature Flag (Optional)

Add configuration flag for gradual rollout:
```cpp
// Config option
bool use_new_hyperliquid_connector = true;  // Default: new connector

if (config_.exchange == "hyperliquid") {
    if (use_new_hyperliquid_connector) {
        auto adapter = std::make_unique<HyperliquidConnectorAdapter>();
        // ...
    } else {
        auto adapter = std::make_unique<HyperliquidAdapter>();  // Old skeleton
        // ...
    }
}
```

#### 4.2 Monitoring Metrics

Add metrics to track:
- Order placement latency (target: < 500μs)
- WebSocket message processing time
- Event callback latency
- Memory pool utilization
- Connection stability

#### 4.3 Logging Enhancements

```cpp
spdlog::info("[HyperliquidConnector] Order placed: {} in {}ns", 
             client_order_id, latency);
spdlog::debug("[HyperliquidConnector] State transition: {} -> {}", 
              old_state, new_state);
spdlog::warn("[HyperliquidConnector] Reconnecting to WebSocket...");
```

---

## 4. Data Flow Diagrams

### 4.1 Order Placement Flow

```
trading_core (Python)
    ↓ ZMQ (ExecutionOrder)
TradingEngineService
    ↓ parse_execution_order_hft()
VenueRouter
    ↓ route by venue
HyperliquidConnectorAdapter (Bridge)
    ↓ translate OrderRequest → OrderParams
HyperliquidPerpetualConnector
    ↓ place_order() → async
    ├── ClientOrderTracker (start tracking)
    └── execute_place_order()
        ↓ HyperliquidAuth::sign_l1_action()
        ↓ HTTP POST to Hyperliquid API
    ← OrderResponse
    ↑ emit_order_created_event()
HyperliquidUserStreamDataSource (WebSocket)
    ↓ on order update
    ↓ process_order_update()
ClientOrderTracker
    ↓ state machine transition
    ↑ callbacks
HyperliquidConnectorAdapter
    ↓ translate to OrderUpdate
    ↑ adapter callback
TradingEngineService::on_order_update_hft()
    ↓ publish ExecutionReport
    ↓ ZMQ PUB
trading_core (receives update)
```

### 4.2 Market Data Flow

```
HyperliquidOrderBookDataSource
    ↓ WebSocket subscription
    ↓ l2Book updates
    ↓ process_orderbook_update()
OrderBookMessage
    ↓ emit_message()
OrderBookTracker (if used)
    ↓ maintain local book
Strategy (trading_core)
```

---

## 5. File Structure

```
latentspeed/
├── include/
│   ├── adapters/
│   │   ├── exchange_adapter.h              # IExchangeAdapter interface
│   │   ├── hyperliquid_adapter.h           # OLD skeleton (to be replaced)
│   │   └── hyperliquid/
│   │       └── hyperliquid_connector_adapter.h  # NEW bridge adapter
│   │
│   └── connector/
│       ├── connector_base.h                # ConnectorBase interface
│       └── exchange/
│           └── hyperliquid/
│               ├── hyperliquid_perpetual_connector.h
│               ├── hyperliquid_order_book_data_source.h
│               ├── hyperliquid_user_stream_data_source.h
│               ├── hyperliquid_marketstream_adapter.h
│               ├── hyperliquid_auth.h
│               └── hyperliquid_web_utils.h
│
├── src/
│   ├── adapters/
│   │   ├── hyperliquid_adapter.cpp         # OLD skeleton (to be replaced)
│   │   └── hyperliquid/
│   │       └── hyperliquid_connector_adapter.cpp  # NEW bridge implementation
│   │
│   ├── connector/
│   │   └── exchange/
│   │       └── hyperliquid/
│   │           ├── hyperliquid_perpetual_connector.cpp
│   │           ├── hyperliquid_order_book_data_source.cpp
│   │           ├── hyperliquid_user_stream_data_source.cpp
│   │           └── hyperliquid_marketstream_adapter.cpp
│   │
│   └── trading_engine_service.cpp          # Modified to use bridge adapter
│
└── tests/
    ├── unit/
    │   └── adapters/
    │       └── test_hyperliquid_connector_adapter.cpp
    └── integration/
        └── test_hyperliquid_engine_integration.cpp
```

---

## 6. Risk Assessment & Mitigation

### 6.1 Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|-----------|
| Translation overhead adds latency | Medium | Low | Benchmark and optimize hot paths |
| Event forwarding race conditions | High | Medium | Proper locking in callback layer |
| Symbol format mismatches | Medium | Medium | Comprehensive translation tests |
| Memory leaks in adapter layer | High | Low | RAII, smart pointers, valgrind tests |
| Breaking existing Bybit/Binance | High | Low | No changes to their adapters |

### 6.2 Rollback Plan

If issues arise:
1. Revert to old `HyperliquidAdapter` skeleton
2. Keep bridge adapter code for future use
3. Debug connector issues separately
4. Re-deploy once resolved

---

## 7. Success Criteria

- [ ] Engine starts successfully with `--exchange hyperliquid`
- [ ] Orders placed via ZMQ complete end-to-end
- [ ] WebSocket updates received and processed
- [ ] Order state transitions work correctly
- [ ] No memory leaks in 24-hour stress test
- [ ] Latency < 500μs for order placement (excluding network)
- [ ] Zero crashes or undefined behavior
- [ ] All unit and integration tests pass

---

## 8. Next Steps

### Immediate Actions (This Week)

1. **Create bridge adapter skeleton**
   - Define class structure
   - Stub out interface methods
   - Set up build system

2. **Implement core translations**
   - OrderRequest → OrderParams
   - Symbol format conversion
   - Order type mapping

3. **Wire event callbacks**
   - Connector events → Adapter callbacks
   - Proper thread-safe forwarding

### Short-term (Next Week)

4. **Integration testing**
   - End-to-end flow testing
   - Latency benchmarking
   - Memory profiling

5. **Documentation**
   - Update architecture docs
   - Add integration examples
   - Create troubleshooting guide

### Long-term (Future Phases)

6. **Migrate other exchanges**
   - Create connectors for Bybit, Binance
   - Unified connector pattern
   - Retire old adapter system

7. **Phase out IExchangeAdapter**
   - Once all exchanges use ConnectorBase
   - Update trading engine to use connectors directly
   - Remove bridge layer

---

## 9. References

- [Exchange Adapter Architecture Plan](EXCHANGE_ADAPTER_ARCHITECTURE_PLAN.md)
- [Refactoring Overview](refactoring/00_OVERVIEW.md)
- [Phase 3 Data Sources](refactoring/04_PHASE3_DATA_SOURCES.md)
- [Hummingbot Trading Lifecycle](HUMMINGBOT_TRADING_LIFECYCLE.md)

---

**Ready to implement?** Start with Phase 1: Create Bridge Adapter!
