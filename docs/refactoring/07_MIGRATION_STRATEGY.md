# Migration Strategy & Timeline

**Total Duration**: 6 weeks  
**Approach**: Incremental, non-breaking migration

---

## Week-by-Week Plan

### Week 1: Phase 1 - Core Architecture

**Goal**: Establish foundation classes

**Tasks**:
- [ ] Create `include/connector/` directory structure
- [ ] Implement `ConnectorBase` abstract class
- [ ] Implement `PerpetualDerivativeBase` class
- [ ] Define `OrderType`, `TradeType`, `PositionAction` enums
- [ ] Create basic `Task<T>` coroutine wrapper
- [ ] Write unit tests for base classes

**Deliverables**:
- `connector_base.h` / `.cpp`
- `perpetual_derivative_base.h` / `.cpp`
- `types.h`
- Unit test suite (80%+ coverage)

**Success Criteria**:
- All tests passing
- Code compiles without warnings
- Documentation generated

---

### Week 2: Phase 2 - Order Tracking

**Goal**: Implement order state management

**Tasks**:
- [ ] Implement `InFlightOrder` class with state machine
- [ ] Implement `ClientOrderTracker` with thread-safe operations
- [ ] Define `OrderUpdate` and `TradeUpdate` structures
- [ ] Create event system (`OrderEventListener`, etc.)
- [ ] Write comprehensive tests for state transitions
- [ ] Test concurrent order tracking

**Deliverables**:
- `in_flight_order.h` / `.cpp`
- `client_order_tracker.h` / `.cpp`
- `events.h`
- Integration tests for order lifecycle

**Success Criteria**:
- State machine validates all transitions
- No race conditions under load test (1000 orders/sec)
- Event emission verified

---

### Week 3: Phase 3 - Data Sources

**Goal**: Separate market data and user data

**Tasks**:
- [ ] Implement `OrderBookTrackerDataSource` abstract class
- [ ] Implement `UserStreamTrackerDataSource` abstract class
- [ ] Create `OrderBook` structure
- [ ] Implement Hyperliquid data sources
- [ ] Write integration tests with mock WebSocket

**Deliverables**:
- `order_book_tracker_data_source.h`
- `user_stream_tracker_data_source.h`
- `order_book.h`
- `hyperliquid_order_book_data_source.h` / `.cpp`
- `hyperliquid_user_stream_data_source.h` / `.cpp`

**Success Criteria**:
- Data sources can run independently
- WebSocket reconnection tested
- Message parsing validated against real API

---

### Week 4: Phase 4 - Auth Modules

**Goal**: Implement exchange-specific authentication

**Tasks**:
- [ ] Implement `HyperliquidAuth` with EIP-712 signing
- [ ] Implement `HyperliquidWebUtils` (float_to_wire, etc.)
- [ ] Set up libsecp256k1 and msgpack dependencies
- [ ] Implement `DydxV4Client` with Cosmos SDK signing
- [ ] Set up gRPC and protobuf dependencies
- [ ] Compile v4-proto definitions
- [ ] Test signature generation against known test vectors

**Deliverables**:
- `hyperliquid_auth.h` / `.cpp`
- `hyperliquid_web_utils.h` / `.cpp`
- `dydx_v4_client.h` / `.cpp`
- `dydx_v4_types.h`
- Auth unit tests

**Success Criteria**:
- Signatures match Hummingbot's output
- Can successfully place test orders on testnet
- Quantums/subticks conversion verified

---

### Week 5: Phase 5 - Event Lifecycle

**Goal**: Complete end-to-end order flow

**Tasks**:
- [ ] Implement `HyperliquidPerpetualConnector`
- [ ] Implement full buy/sell flow with async submission
- [ ] Integrate WebSocket user stream
- [ ] Implement event emission
- [ ] Create `DydxV4PerpetualConnector`
- [ ] Add error handling and retry logic
- [ ] Write end-to-end integration tests

**Deliverables**:
- `hyperliquid_perpetual_connector.h` / `.cpp`
- `dydx_v4_perpetual_connector.h` / `.cpp`
- Full integration test suite

**Success Criteria**:
- Can place, track, and cancel orders
- WebSocket updates processed correctly
- Events emitted in correct order
- No memory leaks in 1-hour stress test

---

### Week 6: Phase 6 - Integration & Migration

**Goal**: Integrate with existing codebase and complete migration

**Tasks**:
- [ ] Create adapter facades for backward compatibility
- [ ] Integrate connectors into `VenueRouter`
- [ ] Update `TradingEngineService` to use new connectors
- [ ] Migrate existing Hyperliquid code
- [ ] Performance testing and optimization
- [ ] Update documentation
- [ ] Create migration guide for users

**Deliverables**:
- Adapter facades (`HyperliquidAdapterFacade`, etc.)
- Updated `TradingEngineService`
- Performance benchmarks
- Migration guide
- Updated README and API docs

**Success Criteria**:
- All existing functionality works
- Performance equals or exceeds old implementation
- Backward compatible API maintained
- Production-ready code

---

## Backward Compatibility Strategy

### Adapter Facade Pattern

Keep existing `IExchangeAdapter` interface and wrap new connectors:

```cpp
class HyperliquidAdapterFacade : public IExchangeAdapter {
public:
    HyperliquidAdapterFacade() {
        connector_ = std::make_unique<HyperliquidPerpetualConnector>(...);
        
        // Set event listener that converts to old callback format
        connector_->set_order_event_listener(this);
    }
    
    // OLD API (blocking)
    OrderResponse place_order(const OrderRequest& req) override {
        // Call new API
        std::string client_order_id = connector_->buy({
            .trading_pair = req.symbol,
            .amount = req.quantity,
            .price = req.price,
            .order_type = map_order_type(req.order_type)
        });
        
        // Block until order reaches OPEN or FAILED state
        auto order = wait_for_order_state(
            client_order_id,
            {OrderState::OPEN, OrderState::FAILED},
            std::chrono::seconds(5)
        );
        
        // Convert to old OrderResponse format
        return OrderResponse{
            .success = (order.current_state == OrderState::OPEN),
            .order_id = order.exchange_order_id.value_or(""),
            .client_order_id = client_order_id,
            .error_message = order.current_state == OrderState::FAILED 
                ? "Order failed" : ""
        };
    }
    
    // Implement other IExchangeAdapter methods...
    
private:
    std::unique_ptr<HyperliquidPerpetualConnector> connector_;
    
    InFlightOrder wait_for_order_state(
        const std::string& client_order_id,
        const std::set<OrderState>& target_states,
        std::chrono::milliseconds timeout
    );
};
```

### Gradual Migration Path

1. **Phase 1**: Keep old adapters alongside new connectors
2. **Phase 2**: Allow users to opt-in to new connectors via config
3. **Phase 3**: Default to new connectors, keep old as fallback
4. **Phase 4**: Deprecate old adapters with 1-month notice
5. **Phase 5**: Remove old code after migration complete

---

## Testing Strategy

### Unit Tests

**Coverage Target**: 85%+

```cpp
// Example: Test order state transitions
TEST(InFlightOrder, StateTransitions) {
    InFlightOrder order{...};
    
    // Valid transitions
    EXPECT_TRUE(can_transition(OrderState::PENDING_CREATE, OrderState::PENDING_SUBMIT));
    EXPECT_TRUE(can_transition(OrderState::PENDING_SUBMIT, OrderState::OPEN));
    EXPECT_TRUE(can_transition(OrderState::OPEN, OrderState::PARTIALLY_FILLED));
    EXPECT_TRUE(can_transition(OrderState::PARTIALLY_FILLED, OrderState::FILLED));
    
    // Invalid transitions
    EXPECT_FALSE(can_transition(OrderState::FILLED, OrderState::OPEN));
    EXPECT_FALSE(can_transition(OrderState::CANCELLED, OrderState::OPEN));
}

// Test concurrent order tracking
TEST(ClientOrderTracker, ConcurrentAccess) {
    ClientOrderTracker tracker;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&tracker, i]() {
            for (int j = 0; j < 100; ++j) {
                InFlightOrder order{...};
                tracker.start_tracking(order);
                // Simulate updates
                tracker.process_order_update({...});
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    // Verify no corruption
    EXPECT_EQ(tracker.active_order_count(), 1000);
}
```

### Integration Tests

**Against Testnet APIs**:

```cpp
TEST(HyperliquidConnector, PlaceAndCancelOrder) {
    HyperliquidPerpetualConnector connector(...);
    connector.initialize();
    connector.connect();
    
    // Place order
    std::string client_order_id = connector.buy({
        .trading_pair = "BTC-USD",
        .amount = 0.001,  // Minimum size for testnet
        .price = 30000.0,
        .order_type = OrderType::LIMIT
    });
    
    // Wait for confirmation
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    auto order = connector.get_order(client_order_id);
    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(order->current_state, OrderState::OPEN);
    EXPECT_TRUE(order->exchange_order_id.has_value());
    
    // Cancel order
    bool cancelled = connector.cancel(client_order_id);
    EXPECT_TRUE(cancelled);
    
    // Wait for cancellation
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    order = connector.get_order(client_order_id);
    EXPECT_EQ(order->current_state, OrderState::CANCELLED);
}
```

### Performance Tests

**Latency Benchmarks**:

```cpp
BENCHMARK(OrderPlacement) {
    HyperliquidPerpetualConnector connector(...);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::string order_id = connector.buy({...});
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    EXPECT_LT(duration.count(), 500);  // < 500μs excluding network
}

BENCHMARK(OrderTracking) {
    ClientOrderTracker tracker;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    tracker.start_tracking(order);
    tracker.process_order_update(update);
    auto result = tracker.get_order(order_id);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    EXPECT_LT(duration.count(), 1000);  // < 1μs
}
```

**Throughput Tests**:

```cpp
TEST(ConnectorPerformance, HighThroughput) {
    HyperliquidPerpetualConnector connector(...);
    
    std::atomic<int> orders_placed{0};
    std::atomic<int> orders_confirmed{0};
    
    auto start = std::chrono::steady_clock::now();
    
    // Place 1000 orders
    for (int i = 0; i < 1000; ++i) {
        connector.buy({...});
        orders_placed++;
    }
    
    // Wait for confirmations
    while (orders_confirmed < 1000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    
    double orders_per_sec = 1000.0 / duration.count();
    EXPECT_GT(orders_per_sec, 100);  // > 100 orders/sec
}
```

**Memory Leak Tests**:

```cpp
TEST(MemoryLeaks, LongRunning) {
    size_t initial_memory = get_memory_usage();
    
    {
        HyperliquidPerpetualConnector connector(...);
        connector.initialize();
        connector.connect();
        
        // Run for 1 hour
        auto end_time = std::chrono::steady_clock::now() + std::chrono::hours(1);
        while (std::chrono::steady_clock::now() < end_time) {
            connector.buy({...});
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        connector.disconnect();
    }
    
    // Force cleanup
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    size_t final_memory = get_memory_usage();
    size_t leaked = final_memory - initial_memory;
    
    EXPECT_LT(leaked, 10 * 1024 * 1024);  // < 10MB leaked
}
```

---

## Rollout Plan

### Phase 1: Internal Testing (Week 5)
- Deploy to dev environment
- Run integration tests
- Performance validation
- Bug fixes

### Phase 2: Beta Testing (Week 6)
- Deploy to staging environment
- Enable for select strategies
- Monitor logs and metrics
- Gather feedback

### Phase 3: Gradual Rollout (Week 7-8)
- 10% of production traffic
- Monitor for issues
- 50% if no issues after 3 days
- 100% if stable for 1 week

### Phase 4: Old Code Deprecation (Week 9-12)
- Mark old adapters as deprecated
- Update documentation
- Provide migration guide
- Remove after 1 month

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Performance regression | Benchmark before/after, optimize hot paths |
| Memory leaks | Valgrind/ASan in CI, long-running tests |
| Race conditions | ThreadSanitizer, stress tests |
| API breaking changes | Adapter facade, feature flags |
| Exchange API changes | Version detection, graceful degradation |
| Data loss | Persist order state to Redis/DB |

---

## Success Metrics

- [ ] Zero production incidents related to refactoring
- [ ] Order placement latency < 500μs (excluding network)
- [ ] Memory usage stable over 24 hours
- [ ] 100% of existing functionality working
- [ ] Code coverage > 85%
- [ ] All benchmarks passing
- [ ] Documentation complete and accurate

---

## Next: File Structure

See [08_FILE_STRUCTURE.md](08_FILE_STRUCTURE.md) for complete directory layout and file organization.
