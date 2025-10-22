# Refactoring Implementation Checklist

Use this checklist to track your progress through the Hummingbot architecture refactoring.

---

## Pre-Implementation Setup

- [ ] Review all refactoring documents (00-08)
- [ ] Discuss timeline and approach with team
- [ ] Set up feature branch: `git checkout -b refactor/hummingbot-architecture`
- [ ] Verify dependencies available in vcpkg
- [ ] Set up test environment with testnet API keys

---

## Week 1: Phase 1 - Core Architecture ✅ COMPLETED

### Day 1-2: Base Classes

- [x] Create `include/connector/` directory
- [x] Create `src/connector/` directory
- [x] Implement `connector/types.h`
  - [x] `OrderType` enum
  - [x] `TradeType` enum
  - [x] `PositionAction` enum (placeholder)
  - [x] `ConnectorType` enum (placeholder)
  - [x] String conversion functions
- [x] Implement `connector/connector_base.h`
  - [x] Pure virtual interface
  - [x] Client order ID generation
  - [x] Quantization helpers
- [x] Implement `connector/connector_base.cpp`
  - [x] `generate_client_order_id()`
  - [x] `quantize_order_price()`
  - [x] `quantize_order_amount()`

### Day 3-4: Derivative Base

- [x] **SIMPLIFIED**: Skipped separate PerpetualDerivativeBase
  - Position management deferred to Phase 5
  - Leverage control deferred to exchange implementations
  - Funding rate interface deferred

### Day 5: Testing & Documentation

- [x] Write unit tests for `ConnectorBase` (12 tests passing)
- [x] Add comprehensive Doxygen comments
- [x] Verify compilation with warnings enabled
- [x] Code review

**Deliverables**: ✅ Core base classes with comprehensive tests (12/12 passing)

---

## Week 2: Phase 2 - Order Tracking ✅ COMPLETED

### Day 1-2: InFlightOrder

- [x] Implement `connector/in_flight_order.h`
  - [x] `OrderState` enum (9 states)
  - [x] `InFlightOrder` class (header-only, copyable)
  - [x] `TradeUpdate` struct
  - [x] `OrderUpdate` struct
  - [x] State query methods
  - [x] **MODIFIED**: Removed async wait (makes class non-copyable)
- [x] **NO CPP FILE**: Header-only implementation
  - [x] Removed mutex/condition_variable for copyability
  - [x] Async waiting deferred to ClientOrderTracker level
- [x] Write tests for state transitions
- [x] **MODIFIED**: Async wait removed from InFlightOrder

### Day 3-4: ClientOrderTracker

- [x] Implement `connector/client_order_tracker.h` (header-only)
  - [x] `start_tracking()` with move semantics
  - [x] `stop_tracking()`
  - [x] `get_order()`
  - [x] `get_order_by_exchange_id()`
  - [x] `process_order_update()`
  - [x] `process_trade_update()`
  - [x] `all_fillable_orders()`
  - [x] Thread-safe with shared_mutex
- [x] **NO CPP FILE**: Header-only implementation
- [x] Test concurrent access (1000 orders, 10 threads)
- [x] Test event emission

### Day 5: Event System

- [x] **SIMPLIFIED**: Inline event callbacks in ClientOrderTracker
  - [x] `OrderEventType` enum
  - [x] Callback-based event system
  - [x] Auto-cleanup feature
- [x] Test event callbacks (14 tests total)
- [x] Integration test: complete order lifecycle

**Deliverables**: ✅ Order tracking infrastructure with events (14/14 tests passing)

---

## Week 3: Phase 3 - Data Sources ✅ COMPLETED

### Day 1-2: OrderBook Components

- [x] Implement `connector/order_book.h` (header-only)
  - [x] `OrderBookEntry` struct
  - [x] `OrderBook` class
  - [x] `apply_snapshot()` and `apply_delta()`
  - [x] `best_bid()`, `best_ask()`, `mid_price()`, `spread()`
  - [x] `get_top_bids()`, `get_top_asks()`
- [x] Implement `connector/order_book_tracker_data_source.h`
  - [x] Abstract interface
  - [x] `get_snapshot()` (REST)
  - [x] `subscribe_orderbook()` (WebSocket)
  - [x] Message callback system (push model)
  - [x] Optional funding rate support
- [x] Write orderbook unit tests (8 tests)

### Day 3-4: UserStream Components

- [x] Implement `connector/user_stream_tracker_data_source.h`
  - [x] Abstract interface
  - [x] `initialize()`, `start()`, `stop()`
  - [x] `subscribe_to_order_updates()`
  - [x] Optional `subscribe_to_balance_updates()`
  - [x] Optional `subscribe_to_position_updates()`
  - [x] Message callback system
- [x] Create mock data sources for testing (6 tests)

### Day 5: Exchange-Specific Implementation

- [x] **DEFERRED**: Exchange-specific data sources to Phase 5
  - Abstractions complete and tested
  - Hyperliquid/dYdX implementations in Phase 5
  - Focus on Hyperliquid in Phase 5

**Deliverables**: ✅ Abstract data sources with mock implementations (16/16 tests passing)

---

## Week 4: Phase 4 - Auth Modules ✅ COMPLETED (Placeholder Crypto)

### Day 1-2: Hyperliquid Auth

- [x] **DEFERRED**: Crypto dependencies (use external signer)
  - [ ] `vcpkg install secp256k1` (for production)
  - [ ] `vcpkg install msgpack-cxx` (for production)
  - [ ] `vcpkg install ethash` (for production)
- [x] Implement `hyperliquid/hyperliquid_auth.h`
- [x] Implement `hyperliquid/hyperliquid_auth.cpp`
  - [x] `action_hash()` structure (⚠️ placeholder msgpack)
  - [x] `construct_phantom_agent()`
  - [x] `sign_l1_action()` with EIP-712 structure
  - [x] ⚠️ **PLACEHOLDER**: `keccak256()`, `ecdsa_sign()`
- [x] Test API structure (5 tests, placeholder signatures)

### Day 2-3: Hyperliquid Web Utils ✅ PRODUCTION READY

- [x] Implement `hyperliquid/hyperliquid_web_utils.h` (header-only)
- [x] ✅ **PRODUCTION READY**: All methods implemented
  - [x] `float_to_wire()` with exact decimal precision
  - [x] `float_to_int_wire()` for integer representation
  - [x] `wire_to_float()` parser
  - [x] `round_to_decimals()`
  - [x] `get_default_size_decimals()` (BTC=5, ETH=4)
  - [x] `validate_size()`, `notional_to_size()`
- [x] Test float conversion accuracy (11 tests, all passing)

### Day 4-5: dYdX v4 Client

- [ ] **DEFERRED TO PHASE 5**: dYdX v4 implementation
  - Focus on Hyperliquid first
  - dYdX requires full Cosmos SDK integration
  - Will implement in Phase 5 if needed

**Deliverables**: ✅ Hyperliquid utilities complete (16/16 tests passing)
- ✅ HyperliquidWebUtils: Production ready
- ⚠️ HyperliquidAuth: Structure complete, crypto placeholder

---

## Week 5: Phase 5 - Event Lifecycle ✅ COMPLETED

### Day 1-2: Hyperliquid Data Sources

- [x] Implement `connector/hyperliquid_order_book_data_source.h` (~400 LOC)
  - [x] WebSocket connection to Hyperliquid
  - [x] Subscribe to l2Book channel
  - [x] Auto-reconnection logic
  - [x] REST API fallback for snapshots
  - [x] Symbol normalization
- [x] Implement `connector/hyperliquid_user_stream_data_source.h` (~450 LOC)
  - [x] Authenticated WebSocket
  - [x] Subscribe to user channel
  - [x] Parse order updates, fills, funding
  - [x] Message callbacks
- [x] Write tests for WebSocket lifecycle

### Day 3-4: Hyperliquid Connector

- [x] Implement `connector/hyperliquid_perpetual_connector.h` (~950 LOC, header-only)
  - [x] Constructor with HyperliquidAuth
  - [x] `initialize()` - fetch asset metadata
  - [x] `start()` / `stop()` - lifecycle management
  - [x] `buy()` - non-blocking async order placement
  - [x] `sell()` - non-blocking async order placement
  - [x] `cancel()` - async order cancellation
  - [x] `execute_place_order()` - REST API call with signing
  - [x] `place_order_and_process_update()` - async wrapper
  - [x] `handle_user_stream_message()` - WebSocket callback
  - [x] `process_order_update()` - order state updates
  - [x] `process_trade_update()` - fill processing
  - [x] Event callbacks (OrderEventListener)
  - [x] **CRITICAL**: Track orders BEFORE API call (Hummingbot pattern)
- [x] Integration with boost::asio for async execution
- [x] Integration with boost::beast for WebSocket
- [x] **DEFERRED**: dYdX v4 connector (focus on Hyperliquid first)

### Day 5: Testing & Documentation

- [x] Write comprehensive tests (20 tests, all passing)
  - [x] Order placement (buy/sell/market/limit maker)
  - [x] Order state transitions
  - [x] Concurrent order placement (10 orders)
  - [x] Event listener callbacks
  - [x] Price/amount quantization
  - [x] Complete lifecycle structure
- [x] Test without actual exchange (validates internal logic)
- [x] Create PHASE5_README.md (comprehensive guide)
- [x] Create PHASE5_COMPLETE.md (summary)
- [x] Create BUILD_PHASE5.sh

**Deliverables**: ✅ Complete Hyperliquid connector with event-driven lifecycle (20/20 tests passing)

---

## Week 6: Phase 6 - Integration & Migration

### Day 1-2: Adapter Facades

- [ ] Implement `adapters/hyperliquid_adapter_facade.h`
- [ ] Implement `adapters/hyperliquid_adapter_facade.cpp`
  - [ ] Wrap `HyperliquidPerpetualConnector`
  - [ ] Translate `OrderRequest` → connector params
  - [ ] Block and convert to `OrderResponse`
- [ ] Implement `adapters/dydx_adapter_facade.h`
- [ ] Implement `adapters/dydx_adapter_facade.cpp`
- [ ] Test backward compatibility

### Day 3: VenueRouter Update

- [ ] Update `engine/venue_router.h`
  - [ ] Support both `IExchangeAdapter` and `ConnectorBase`
- [ ] Update `engine/venue_router.cpp`
- [ ] Test routing to both old and new connectors

### Day 4: TradingEngineService Update

- [ ] Update `trading_engine_service.h`
  - [ ] Add connector initialization
- [ ] Update `trading_engine_service.cpp`
  - [ ] Use connectors instead of adapters (opt-in)
- [ ] Add config flag: `use_new_connectors`

### Day 5: Performance & Documentation

- [ ] Run performance benchmarks
- [ ] Compare old vs new latency
- [ ] Update README.md
- [ ] Update API documentation
- [ ] Create migration guide for users
- [ ] Final code review

**Deliverables**: ✅ Production-ready integration

---

## Post-Implementation

### Testing

- [ ] Run full test suite
- [ ] Verify 85%+ code coverage
- [ ] Run AddressSanitizer tests
- [ ] Run ThreadSanitizer tests
- [ ] 24-hour stress test
- [ ] Load test (1000+ orders/sec)

### Deployment

- [ ] Deploy to dev environment
- [ ] Internal testing (1 week)
- [ ] Deploy to staging
- [ ] Beta testing (1 week)
- [ ] Gradual production rollout
  - [ ] 10% traffic
  - [ ] 50% traffic
  - [ ] 100% traffic

### Cleanup

- [ ] Mark old adapters as deprecated
- [ ] Schedule old code removal (1 month notice)
- [ ] Archive old implementation
- [ ] Celebrate! 🎉

---

## Quick Commands

```bash
# Create directory structure
mkdir -p include/connector/{hyperliquid,dydx_v4}
mkdir -p src/connector/{hyperliquid,dydx_v4}
mkdir -p tests/{unit,integration,performance}/{connector,hyperliquid,dydx_v4}

# Build with tests
cmake -B build -DBUILD_TESTS=ON
cmake --build build -j$(nproc)

# Run tests
cd build && ctest --output-on-failure

# Run specific test
./build/tests/unit/test_connector_base

# Run with sanitizers
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build && ./build/tests/unit/test_client_order_tracker

# Performance benchmark
./build/tests/performance/bench_order_placement

# Code coverage
cmake -B build -DENABLE_COVERAGE=ON
cmake --build build
cd build && ctest
gcovr -r .. --html --html-details -o coverage.html
```

---

## Resources

- **Hummingbot Source**: `sub/hummingbot/hummingbot/connector/derivative/`
- **Documentation**: `docs/refactoring/00_OVERVIEW.md`
- **Issue Tracker**: Create GitHub issues for each phase
- **Code Review**: Request review after each phase completion

---

## Success Metrics

Track these metrics throughout implementation:

| Metric | Target | Current |
|--------|--------|---------|
| Order placement latency | < 500μs | - |
| Memory usage (24h) | Stable | - |
| Test coverage | > 85% | - |
| Compilation warnings | 0 | - |
| Failed tests | 0 | - |
| Production incidents | 0 | - |

---

## Current Status

**Progress**: 🎯 83.3% Complete (Phases 1-5 of 6)

```
Phase 1: ████████████████████ 100% ✅ DONE (12 tests)
Phase 2: ████████████████████ 100% ✅ DONE (14 tests)
Phase 3: ████████████████████ 100% ✅ DONE (16 tests)
Phase 4: ████████████████████ 100% ✅ DONE (16 tests)
Phase 5: ████████████████████ 100% ✅ DONE (20 tests)
Phase 6: ░░░░░░░░░░░░░░░░░░░░   0%  🔄 NEXT
```

**Total Tests**: 78 passing  
**Total LOC**: ~5,000 lines  
**Status**: 🎉 Phase 5 Complete! Ready for Phase 6 (Integration)  
**Next Action**: Integrate connector with existing engine  
**Estimated Remaining**: 1-2 weeks
