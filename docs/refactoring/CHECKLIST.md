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

## Week 1: Phase 1 - Core Architecture

### Day 1-2: Base Classes

- [ ] Create `include/connector/` directory
- [ ] Create `src/connector/` directory
- [ ] Implement `connector/types.h`
  - [ ] `OrderType` enum
  - [ ] `TradeType` enum
  - [ ] `PositionAction` enum
  - [ ] `ConnectorType` enum
  - [ ] String conversion functions
- [ ] Implement `connector/connector_base.h`
  - [ ] Pure virtual interface
  - [ ] Client order ID generation
  - [ ] Quantization helpers
- [ ] Implement `connector/connector_base.cpp`
  - [ ] `generate_client_order_id()`
  - [ ] `quantize_order_price()`
  - [ ] `quantize_order_amount()`

### Day 3-4: Derivative Base

- [ ] Implement `connector/perpetual_derivative_base.h`
  - [ ] Position management interface
  - [ ] Leverage control interface
  - [ ] Funding rate interface
- [ ] Implement `connector/perpetual_derivative_base.cpp`
  - [ ] `update_position()`
  - [ ] `update_funding_rate()`

### Day 5: Testing & Documentation

- [ ] Write unit tests for `ConnectorBase`
- [ ] Write unit tests for `PerpetualDerivativeBase`
- [ ] Add Doxygen comments
- [ ] Verify compilation with `-Wall -Werror`
- [ ] Code review

**Deliverables**: ✅ Core base classes with 80%+ test coverage

---

## Week 2: Phase 2 - Order Tracking

### Day 1-2: InFlightOrder

- [ ] Implement `connector/in_flight_order.h`
  - [ ] `OrderState` enum
  - [ ] `InFlightOrder` class
  - [ ] `TradeUpdate` struct
  - [ ] `OrderUpdate` struct
  - [ ] State query methods
  - [ ] Async exchange order ID wait
- [ ] Implement `connector/in_flight_order.cpp`
  - [ ] `get_exchange_order_id_async()`
  - [ ] `notify_exchange_order_id_ready()`
- [ ] Write tests for state transitions
- [ ] Test async wait functionality

### Day 3-4: ClientOrderTracker

- [ ] Implement `connector/client_order_tracker.h`
  - [ ] `start_tracking()`
  - [ ] `stop_tracking()`
  - [ ] `get_order()`
  - [ ] `get_order_by_exchange_id()`
  - [ ] `process_order_update()`
  - [ ] `process_trade_update()`
  - [ ] `all_fillable_orders()`
- [ ] Implement `connector/client_order_tracker.cpp`
- [ ] Test concurrent access (ThreadSanitizer)
- [ ] Test event emission

### Day 5: Event System

- [ ] Implement `connector/events.h`
  - [ ] `OrderEventType` enum
  - [ ] `OrderEventListener` interface
  - [ ] `TradeEventListener` interface
  - [ ] `ErrorEventListener` interface
- [ ] Create mock listeners for testing
- [ ] Integration test: order lifecycle with events

**Deliverables**: ✅ Order tracking infrastructure with event system

---

## Week 3: Phase 3 - Data Sources

### Day 1-2: OrderBook Components

- [ ] Implement `connector/order_book.h`
  - [ ] `OrderBookEntry` struct
  - [ ] `OrderBook` class
  - [ ] `apply_diff()`
  - [ ] `best_bid()`, `best_ask()`, `mid_price()`
- [ ] Implement `connector/order_book_tracker_data_source.h`
  - [ ] Abstract interface
  - [ ] `get_snapshot()`
  - [ ] `listen_for_order_book_diffs()`
  - [ ] Message callback system
- [ ] Write orderbook unit tests

### Day 3-4: UserStream Components

- [ ] Implement `connector/user_stream_tracker_data_source.h`
  - [ ] Abstract interface
  - [ ] `listen_for_user_stream()`
  - [ ] `subscribe_to_order_updates()`
  - [ ] `subscribe_to_balance_updates()`
  - [ ] Message callback system
- [ ] Create mock data sources for testing

### Day 5: Hyperliquid Data Sources

- [ ] Implement `hyperliquid/hyperliquid_order_book_data_source.h`
- [ ] Implement `hyperliquid/hyperliquid_order_book_data_source.cpp`
  - [ ] WebSocket connection
  - [ ] Subscribe to l2Book
  - [ ] Parse orderbook messages
- [ ] Implement `hyperliquid/hyperliquid_user_stream_data_source.h`
- [ ] Implement `hyperliquid/hyperliquid_user_stream_data_source.cpp`
  - [ ] Subscribe to userOrders
  - [ ] Subscribe to userEvents
  - [ ] Parse order/fill messages
- [ ] Integration test with Hyperliquid testnet

**Deliverables**: ✅ Separate market and user data sources

---

## Week 4: Phase 4 - Auth Modules

### Day 1-2: Hyperliquid Auth

- [ ] Install dependencies
  - [ ] `vcpkg install secp256k1`
  - [ ] `vcpkg install msgpack-cxx`
  - [ ] `vcpkg install ethash`
- [ ] Implement `hyperliquid/hyperliquid_auth.h`
- [ ] Implement `hyperliquid/hyperliquid_auth.cpp`
  - [ ] `action_hash()` with msgpack
  - [ ] `construct_phantom_agent()`
  - [ ] `sign_l1_action()` with EIP-712
  - [ ] `sign_order_params()`
  - [ ] `sign_cancel_params()`
- [ ] Test signature generation against known vectors

### Day 2-3: Hyperliquid Web Utils

- [ ] Implement `hyperliquid/hyperliquid_web_utils.h`
- [ ] Implement `hyperliquid/hyperliquid_web_utils.cpp`
  - [ ] `float_to_wire()` with 8 decimal precision
  - [ ] `order_spec_to_order_wire()`
  - [ ] `order_type_to_wire()`
  - [ ] `float_to_int_for_hashing()`
- [ ] Test float conversion accuracy

### Day 4-5: dYdX v4 Client

- [ ] Install dependencies
  - [ ] `vcpkg install grpc`
  - [ ] `vcpkg install protobuf`
  - [ ] Clone and compile v4-proto
- [ ] Implement `dydx_v4/private_key.h/cpp`
  - [ ] Mnemonic → private key derivation
- [ ] Implement `dydx_v4/transaction.h/cpp`
  - [ ] Cosmos SDK transaction builder
- [ ] Implement `dydx_v4/dydx_v4_client.h`
- [ ] Implement `dydx_v4/dydx_v4_client.cpp`
  - [ ] gRPC channel setup
  - [ ] `calculate_quantums()`
  - [ ] `calculate_subticks()`
  - [ ] `place_order()` with retry
  - [ ] `prepare_and_broadcast_transaction()`
- [ ] Test quantums/subticks conversion
- [ ] Test transaction signing
- [ ] Test against dYdX testnet

**Deliverables**: ✅ Exchange-specific auth with verified signatures

---

## Week 5: Phase 5 - Event Lifecycle

### Day 1-2: Hyperliquid Connector

- [ ] Implement `hyperliquid/hyperliquid_perpetual_connector.h`
- [ ] Implement `hyperliquid/hyperliquid_perpetual_connector.cpp`
  - [ ] Constructor with auth initialization
  - [ ] `initialize()` - fetch asset metadata
  - [ ] `connect()` - start data sources
  - [ ] `buy()` - async order placement
  - [ ] `sell()` - async order placement
  - [ ] `cancel()` - cancel order
  - [ ] `_place_order()` - actual API call
  - [ ] `_place_order_and_process_update()` - wrapper
  - [ ] `_user_stream_event_listener()` - WebSocket loop
  - [ ] `process_order_message_from_ws()`
  - [ ] `process_trade_message_from_ws()`
- [ ] Test order placement on testnet
- [ ] Test order cancellation
- [ ] Test WebSocket updates

### Day 3-4: dYdX v4 Connector

- [ ] Implement `dydx_v4/dydx_v4_perpetual_connector.h`
- [ ] Implement `dydx_v4/dydx_v4_perpetual_connector.cpp`
  - [ ] Similar structure to Hyperliquid
  - [ ] Market metadata caching
  - [ ] Price adjustment for market orders
  - [ ] Integration with dYdX client
- [ ] Test on dYdX testnet
- [ ] Test sequence mismatch retry

### Day 5: End-to-End Testing

- [ ] Full order lifecycle test (Hyperliquid)
- [ ] Full order lifecycle test (dYdX v4)
- [ ] Test concurrent orders
- [ ] Test error scenarios
- [ ] Memory leak test (Valgrind)

**Deliverables**: ✅ Complete connectors with event-driven lifecycle

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

**Status**: 📋 Ready to Start  
**Next Action**: Review Phase 1 documents and begin Day 1 tasks  
**Estimated Completion**: 6 weeks from start date
