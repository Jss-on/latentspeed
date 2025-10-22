# Complete File Structure

This document outlines the complete directory and file structure for the refactored codebase.

---

## Directory Tree

```
latentspeed/
├── include/
│   ├── connector/                          # ✅ NEW: Connector framework (Phase 1-4)
│   │   ├── connector_base.h                # ✅ Phase 1: Abstract base
│   │   ├── types.h                         # ✅ Phase 1: Common enums and types
│   │   ├── in_flight_order.h               # ✅ Phase 2: Order state machine (header-only)
│   │   ├── client_order_tracker.h          # ✅ Phase 2: Order tracking (header-only)
│   │   ├── order_book.h                    # ✅ Phase 3: OrderBook structure (header-only)
│   │   ├── order_book_tracker_data_source.h    # ✅ Phase 3: Market data abstraction
│   │   ├── user_stream_tracker_data_source.h   # ✅ Phase 3: User data abstraction
│   │   ├── hyperliquid_auth.h              # ✅ Phase 4: EIP-712 signing (placeholder crypto)
│   │   ├── hyperliquid_web_utils.h         # ✅ Phase 4: Float conversion (production ready)
│   │   │
│   │   ├── DEFERRED TO PHASE 5:
│   │   │   ├── perpetual_derivative_base.h     # Derivatives base (optional)
│   │   │   ├── spot_exchange_base.h            # Spot exchanges (future)
│   │   │   ├── position.h                      # Position representation
│   │   │   ├── trading_rule.h                  # Exchange trading rules
│   │   │
│   │   └── hyperliquid/                    # Phase 5: Exchange implementation
│   │       ├── hyperliquid_perpetual_connector.h
│   │       ├── hyperliquid_order_book_data_source.h
│   │       ├── hyperliquid_user_stream_data_source.h
│   │       ├── hyperliquid_constants.h
│   │       └── hyperliquid_types.h
│   │   │
│   │   └── dydx_v4/                        # dYdX v4-specific
│   │       ├── dydx_v4_perpetual_connector.h
│   │       ├── dydx_v4_client.h
│   │       ├── dydx_v4_types.h
│   │       ├── dydx_v4_constants.h
│   │       ├── dydx_v4_order_book_data_source.h
│   │       ├── dydx_v4_user_stream_data_source.h
│   │       ├── private_key.h               # Cosmos key derivation
│   │       └── transaction.h               # Cosmos transaction builder
│   │
│   ├── adapters/                           # EXISTING: Adapter facades (Phase 1)
│   │   ├── exchange_adapter.h              # Keep for backward compat
│   │   ├── bybit_adapter.h
│   │   ├── binance_adapter.h
│   │   ├── hyperliquid_adapter.h           # Facade wrapping new connector
│   │   └── dydx_adapter.h                  # Facade wrapping new connector
│   │
│   ├── engine/                             # EXISTING: Engine components
│   │   ├── venue_router.h                  # Update to support both
│   │   ├── exec_dto.h
│   │   └── normalized_order.h
│   │
│   ├── core/                               # EXISTING: Core utilities
│   │   ├── auth/
│   │   ├── symbol/
│   │   └── reasons/
│   │
│   ├── exchange/                           # EXISTING: Legacy exchange clients
│   │   ├── exchange_client.h               # Keep for now
│   │   ├── bybit_client.h
│   │   └── binance_client.h
│   │
│   ├── hft_data_structures.h               # EXISTING: Keep (excellent!)
│   ├── trading_engine_service.h            # EXISTING: Update to use connectors
│   ├── market_data_provider.h              # EXISTING: Keep
│   ├── feed_handler.h                      # EXISTING: Keep
│   └── exchange_interface.h                # EXISTING: Keep for market data
│
├── src/
│   ├── connector/                          # ✅ Connector implementations (Phase 1-4)
│   │   ├── connector_base.cpp              # ✅ Phase 1
│   │   ├── hyperliquid_auth.cpp            # ✅ Phase 4 (placeholder crypto)
│   │   │
│   │   ├── HEADER-ONLY (No .cpp files):
│   │   │   ├── in_flight_order.h           # Phase 2: Copyable data structure
│   │   │   ├── client_order_tracker.h      # Phase 2: Thread-safe tracking
│   │   │   ├── order_book.h                # Phase 3: Orderbook
│   │   │   ├── hyperliquid_web_utils.h     # Phase 4: Float conversion
│   │   │   ├── order_book_tracker_data_source.h   # Phase 3: Abstract interface
│   │   │   └── user_stream_tracker_data_source.h  # Phase 3: Abstract interface
│   │   │
│   │   └── hyperliquid/                    # Phase 5: Exchange implementation
│   │       ├── hyperliquid_perpetual_connector.cpp
│   │       ├── hyperliquid_order_book_data_source.cpp
│   │       └── hyperliquid_user_stream_data_source.cpp
│   │   │
│   │   └── dydx_v4/
│   │       ├── dydx_v4_perpetual_connector.cpp
│   │       ├── dydx_v4_client.cpp
│   │       ├── dydx_v4_order_book_data_source.cpp
│   │       ├── dydx_v4_user_stream_data_source.cpp
│   │       ├── private_key.cpp
│   │       └── transaction.cpp
│   │
│   ├── adapters/                           # EXISTING: Adapter implementations
│   │   ├── bybit_adapter.cpp
│   │   ├── binance_adapter.cpp
│   │   ├── hyperliquid_adapter_facade.cpp  # NEW: Facade
│   │   └── dydx_adapter_facade.cpp         # NEW: Facade
│   │
│   ├── engine/
│   │   ├── venue_router.cpp
│   │   └── exec_dto.cpp
│   │
│   ├── exchange/                           # EXISTING: Legacy clients
│   │   ├── bybit_client.cpp
│   │   └── binance_client.cpp
│   │
│   ├── trading_engine_service.cpp          # EXISTING: Update
│   ├── market_data_provider.cpp            # EXISTING: Keep
│   ├── feed_handler.cpp                    # EXISTING: Keep
│   ├── exchange_interface.cpp              # EXISTING: Keep
│   ├── main.cpp                            # EXISTING: Update
│   └── marketstream.cpp                    # EXISTING: Keep
│
├── tests/                                  # ✅ Comprehensive test suite (Phases 1-4)
│   ├── unit/
│   │   ├── connector/
│   │   │   ├── test_connector_base.cpp           # ✅ Phase 1 (12 tests)
│   │   │   ├── test_order_tracking.cpp           # ✅ Phase 2 (14 tests)
│   │   │   ├── test_order_book.cpp               # ✅ Phase 3 (16 tests)
│   │   │   └── test_hyperliquid_utils.cpp        # ✅ Phase 4 (16 tests)
│   │   │
│   │   └── DEFERRED TO PHASE 5:
│   │       ├── test_hyperliquid_connector.cpp
│   │       └── test_dydx_v4_client.cpp
│   │
│   ├── integration/
│   │   ├── test_hyperliquid_connector.cpp  # Against testnet
│   │   ├── test_dydx_v4_connector.cpp
│   │   ├── test_order_lifecycle.cpp
│   │   └── test_websocket_updates.cpp
│   │
│   ├── performance/
│   │   ├── bench_order_placement.cpp
│   │   ├── bench_order_tracking.cpp
│   │   └── bench_memory.cpp
│   │
│   └── mocks/
│       ├── mock_exchange_api.h
│       ├── mock_websocket.h
│       └── mock_order_book_data_source.h
│
├── docs/                                   # Documentation
│   ├── refactoring/                        # THIS DOCUMENT SET
│   │   ├── 00_OVERVIEW.md
│   │   ├── 01_CURRENT_STATE.md
│   │   ├── 02_PHASE1_CORE_ARCHITECTURE.md
│   │   ├── 03_PHASE2_ORDER_TRACKING.md
│   │   ├── 04_PHASE3_DATA_SOURCES.md
│   │   ├── 05_PHASE4_AUTH_MODULES.md
│   │   ├── 06_PHASE5_EVENT_LIFECYCLE.md
│   │   ├── 07_MIGRATION_STRATEGY.md
│   │   └── 08_FILE_STRUCTURE.md
│   │
│   ├── api/                                # API documentation
│   │   ├── connector_api.md
│   │   ├── event_system.md
│   │   └── order_tracking.md
│   │
│   ├── examples/                           # Usage examples
│   │   ├── simple_order_placement.md
│   │   ├── event_driven_strategy.md
│   │   └── multi_exchange_routing.md
│   │
│   ├── HYPERLIQUID_INTEGRATION.md          # EXISTING
│   ├── QUICKSTART_HYPERLIQUID.md           # EXISTING
│   └── README.md                           # Main documentation
│
├── examples/                               # Code examples
│   ├── connector/                          # NEW
│   │   ├── hyperliquid_simple.cpp
│   │   ├── dydx_v4_simple.cpp
│   │   ├── event_listener_example.cpp
│   │   └── multi_exchange_strategy.cpp
│   │
│   ├── hyperliquid_example.cpp             # EXISTING: Keep
│   └── marketstream_example.cpp            # EXISTING: Keep
│
├── configs/                                # Configuration files
│   ├── connector_hyperliquid.yml           # NEW
│   ├── connector_dydx_v4.yml               # NEW
│   ├── config_bybit.yml                    # EXISTING
│   └── config_binance.yml                  # EXISTING
│
├── external/                               # Third-party dependencies
│   ├── vcpkg/                              # EXISTING
│   └── v4-proto/                           # NEW: dYdX protobuf definitions
│
├── tools/                                  # Development tools
│   ├── code_generator/                     # NEW: Generate connector boilerplate
│   └── test_helpers/                       # NEW: Test utilities
│
├── CMakeLists.txt                          # EXISTING: Update
├── vcpkg.json                              # EXISTING: Add new dependencies
└── README.md                               # EXISTING: Update
```

---

## Key Files by Component

### 1. Core Connector Framework (✅ Phases 1-4 Complete)

| File | LOC | Status | Description |
|------|-----|--------|-------------|
| `connector/connector_base.h` | ~200 | ✅ Phase 1 | Abstract base defining connector contract |
| `connector/types.h` | ~80 | ✅ Phase 1 | Common enums and type definitions |
| `connector/in_flight_order.h` | ~185 | ✅ Phase 2 | Order state machine (header-only, copyable) |
| `connector/client_order_tracker.h` | ~310 | ✅ Phase 2 | Centralized order tracking (header-only) |
| `connector/order_book.h` | ~215 | ✅ Phase 3 | OrderBook structure (header-only) |
| `connector/order_book_tracker_data_source.h` | ~130 | ✅ Phase 3 | Market data abstraction |
| `connector/user_stream_tracker_data_source.h` | ~110 | ✅ Phase 3 | User data abstraction |
| `connector/hyperliquid_auth.h` | ~180 | ✅ Phase 4 | EIP-712 signing (placeholder crypto) |
| `connector/hyperliquid_web_utils.h` | ~180 | ✅ Phase 4 | Float conversion (production ready) |

**Total Header LOC**: ~1,590
**Implementation LOC**: ~350 (connector_base.cpp + hyperliquid_auth.cpp)
**Total**: ~1,940 LOC

### 2. Hyperliquid Implementation (⏳ Phase 5 - In Progress)

| File | LOC | Status | Description |
|------|-----|--------|-------------|
| `connector/hyperliquid_auth.h` | ~180 | ✅ Phase 4 | EIP-712 signing structure |
| `connector/hyperliquid_auth.cpp` | ~150 | ✅ Phase 4 | Auth impl (placeholder crypto) |
| `connector/hyperliquid_web_utils.h` | ~180 | ✅ Phase 4 | Float conversion (production ready) |
| `connector/hyperliquid/hyperliquid_perpetual_connector.h` | ~200 | ⏳ Phase 5 | Main connector class |
| `connector/hyperliquid/hyperliquid_perpetual_connector.cpp` | ~800 | ⏳ Phase 5 | Connector implementation |
| `connector/hyperliquid/hyperliquid_order_book_data_source.cpp` | ~400 | ⏳ Phase 5 | Market data source |
| `connector/hyperliquid/hyperliquid_user_stream_data_source.cpp` | ~350 | ⏳ Phase 5 | User data source |

**Completed (Phase 4)**: ~510 LOC
**Remaining (Phase 5)**: ~1,750 LOC

### 3. dYdX v4 Implementation (❌ Deferred)

**Status**: ❌ Deferred - Not in current scope
- Focus on Hyperliquid first
- dYdX v4 requires full Cosmos SDK integration
- Will implement if needed after Phase 5 complete

### 4. Tests (✅ Phases 1-4 Complete)

| File | Tests | LOC | Status | Description |
|------|-------|-----|--------|-------------|
| `tests/unit/connector/test_connector_base.cpp` | 12 | ~230 | ✅ Phase 1 | ConnectorBase tests |
| `tests/unit/connector/test_order_tracking.cpp` | 14 | ~380 | ✅ Phase 2 | Order tracking tests |
| `tests/unit/connector/test_order_book.cpp` | 16 | ~370 | ✅ Phase 3 | OrderBook & data sources |
| `tests/unit/connector/test_hyperliquid_utils.cpp` | 16 | ~280 | ✅ Phase 4 | Hyperliquid utils tests |

**Total Tests**: 58 passing
**Total Test LOC**: ~1,260
**Coverage**: Core framework and utilities fully tested

---

## Dependencies Update

### vcpkg.json

```json
{
  "name": "latentspeed",
  "version": "2.0.0",
  "dependencies": [
    "boost",
    "spdlog",
    "nlohmann-json",
    "cppzmq",
    "websocketpp",
    "openssl",
    "rapidjson",
    
    // NEW: For Hyperliquid
    "ethash",          // Keccak256 hashing
    "secp256k1",       // ECDSA signing
    "msgpack-cxx",     // msgpack serialization
    
    // NEW: For dYdX v4
    "grpc",            // gRPC client
    "protobuf",        // Protobuf serialization
    "abseil",          // Required by gRPC
    
    // Testing
    "gtest",
    "benchmark"
  ]
}
```

### CMakeLists.txt Updates

```cmake
# New targets
add_library(connector_framework
    src/connector/connector_base.cpp
    src/connector/perpetual_derivative_base.cpp
    src/connector/in_flight_order.cpp
    src/connector/client_order_tracker.cpp
    src/connector/order_book.cpp
)

add_library(hyperliquid_connector
    src/connector/hyperliquid/hyperliquid_perpetual_connector.cpp
    src/connector/hyperliquid/hyperliquid_auth.cpp
    src/connector/hyperliquid/hyperliquid_web_utils.cpp
    src/connector/hyperliquid/hyperliquid_order_book_data_source.cpp
    src/connector/hyperliquid/hyperliquid_user_stream_data_source.cpp
)

add_library(dydx_v4_connector
    src/connector/dydx_v4/dydx_v4_perpetual_connector.cpp
    src/connector/dydx_v4/dydx_v4_client.cpp
    src/connector/dydx_v4/dydx_v4_order_book_data_source.cpp
    src/connector/dydx_v4/dydx_v4_user_stream_data_source.cpp
    src/connector/dydx_v4/private_key.cpp
    src/connector/dydx_v4/transaction.cpp
)

# Link dependencies
target_link_libraries(hyperliquid_connector
    PRIVATE
        connector_framework
        secp256k1
        msgpack-cxx
        ethash
)

target_link_libraries(dydx_v4_connector
    PRIVATE
        connector_framework
        gRPC::grpc++
        protobuf::libprotobuf
)
```

---

## Line Count Actuals (Phases 1-4 Complete)

| Component | Header LOC | Source LOC | Test LOC | Total | Status |
|-----------|------------|------------|----------|-------|--------|
| **Phase 1**: Core Base | ~280 | ~200 | ~230 | ~710 | ✅ DONE |
| **Phase 2**: Order Tracking | ~495 | 0 | ~380 | ~875 | ✅ DONE |
| **Phase 3**: Data Sources | ~455 | 0 | ~370 | ~825 | ✅ DONE |
| **Phase 4**: Hyperliquid Utils | ~360 | ~150 | ~280 | ~790 | ✅ DONE |
| **COMPLETED TOTAL** | **~1,590** | **~350** | **~1,260** | **~3,200** | ✅ 66.7% |
| | | | | |
| **Phase 5**: Event Lifecycle | ~600 | ~1,500 | ~400 | ~2,500 | ⏳ NEXT |
| **Phase 6**: Integration | ~200 | ~400 | ~300 | ~900 | 🔜 PENDING |
| **REMAINING TOTAL** | **~800** | **~1,900** | **~700** | **~3,400** | 📋 33.3% |
| | | | | |
| **GRAND TOTAL** | **~2,390** | **~2,250** | **~1,960** | **~6,600** | 🎯 TARGET |

---

## Migration Impact on Existing Files

### Modified Files

| File | Changes | Est. LOC Changed |
|------|---------|------------------|
| `trading_engine_service.cpp` | Add connector support | ~200 |
| `venue_router.h/cpp` | Support both adapters and connectors | ~50 |
| `main.cpp` | Initialize connectors | ~30 |
| `CMakeLists.txt` | Add new targets | ~100 |
| `vcpkg.json` | Add dependencies | ~10 |

**Total Existing Code Modified**: ~390 LOC

### Deprecated Files (Keep for now)

- `adapters/hyperliquid_adapter.h/cpp` - Replace with facade
- `exchange/hyperliquid_client.h/cpp` - No longer needed

---

## Summary

### Completed (Phases 1-4)
- **New Code**: ~3,200 LOC
- **Tests**: 58 passing (12+14+16+16)
- **Modified Existing**: ~100 LOC (CMakeLists, vcpkg.json)
- **Status**: ✅ Core framework, order tracking, data sources, utilities complete

### Key Achievements
✅ **Production Ready**:
- ConnectorBase with client order ID generation
- InFlightOrder with 9-state machine
- ClientOrderTracker with thread-safe tracking
- OrderBook with L2 data management
- HyperliquidWebUtils with exact float precision

⚠️ **Placeholder**:
- HyperliquidAuth (structure complete, crypto needs implementation or external signer)

### Remaining (Phases 5-6)
- **Phase 5**: Event-driven order lifecycle (~2,500 LOC)
- **Phase 6**: Integration & migration (~900 LOC)
- **Total Remaining**: ~3,400 LOC

### Design Decisions
1. **Header-only for simplicity**: Most classes are header-only for performance
2. **Copyable data structures**: Removed mutexes from InFlightOrder
3. **Move semantics**: Used for efficient order tracking
4. **External crypto**: Deferred crypto implementation (use Python/TypeScript signer)
5. **Simplified scope**: Focused on Hyperliquid, deferred dYdX v4

---

**Current Status**: 🎯 66.7% Complete
**Next Phase**: Phase 5 - Event-Driven Order Lifecycle
**Estimated Time**: 1-2 weeks

🚀 **Ready for Phase 5!**
