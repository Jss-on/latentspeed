# Complete File Structure

This document outlines the complete directory and file structure for the refactored codebase.

---

## Directory Tree

```
latentspeed/
├── include/
│   ├── connector/                          # NEW: Connector framework
│   │   ├── connector_base.h                # Abstract base for all connectors
│   │   ├── perpetual_derivative_base.h     # Base for perpetual derivatives
│   │   ├── spot_exchange_base.h            # Base for spot exchanges (future)
│   │   ├── types.h                         # Common enums and types
│   │   ├── events.h                        # Event listener interfaces
│   │   ├── in_flight_order.h               # Order state machine
│   │   ├── client_order_tracker.h          # Centralized order tracking
│   │   ├── order_book.h                    # OrderBook structure
│   │   ├── position.h                      # Position representation
│   │   ├── trading_rule.h                  # Exchange trading rules
│   │   ├── order_book_tracker_data_source.h    # Market data abstraction
│   │   ├── user_stream_tracker_data_source.h   # User data abstraction
│   │   │
│   │   ├── hyperliquid/                    # Hyperliquid-specific
│   │   │   ├── hyperliquid_perpetual_connector.h
│   │   │   ├── hyperliquid_auth.h
│   │   │   ├── hyperliquid_web_utils.h
│   │   │   ├── hyperliquid_constants.h
│   │   │   ├── hyperliquid_order_book_data_source.h
│   │   │   ├── hyperliquid_user_stream_data_source.h
│   │   │   └── hyperliquid_types.h
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
│   ├── connector/                          # NEW: Connector implementations
│   │   ├── connector_base.cpp
│   │   ├── perpetual_derivative_base.cpp
│   │   ├── in_flight_order.cpp
│   │   ├── client_order_tracker.cpp
│   │   ├── order_book.cpp
│   │   ├── position.cpp
│   │   ├── trading_rule.cpp
│   │   │
│   │   ├── hyperliquid/
│   │   │   ├── hyperliquid_perpetual_connector.cpp
│   │   │   ├── hyperliquid_auth.cpp
│   │   │   ├── hyperliquid_web_utils.cpp
│   │   │   ├── hyperliquid_order_book_data_source.cpp
│   │   │   └── hyperliquid_user_stream_data_source.cpp
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
├── tests/                                  # NEW: Comprehensive test suite
│   ├── unit/
│   │   ├── connector/
│   │   │   ├── test_connector_base.cpp
│   │   │   ├── test_in_flight_order.cpp
│   │   │   ├── test_client_order_tracker.cpp
│   │   │   ├── test_order_book.cpp
│   │   │   └── test_state_transitions.cpp
│   │   ├── hyperliquid/
│   │   │   ├── test_hyperliquid_auth.cpp
│   │   │   ├── test_hyperliquid_web_utils.cpp
│   │   │   └── test_float_to_wire.cpp
│   │   └── dydx_v4/
│   │       ├── test_dydx_v4_client.cpp
│   │       ├── test_quantums_subticks.cpp
│   │       └── test_cosmos_signing.cpp
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

### 1. Core Connector Framework

| File | LOC | Description |
|------|-----|-------------|
| `connector/connector_base.h` | ~300 | Abstract base defining connector contract |
| `connector/perpetual_derivative_base.h` | ~150 | Derivative-specific base class |
| `connector/in_flight_order.h` | ~200 | Order state machine and tracking |
| `connector/client_order_tracker.h` | ~250 | Centralized order tracking |
| `connector/types.h` | ~100 | Common enums and type definitions |
| `connector/events.h` | ~150 | Event listener interfaces |

**Total**: ~1,150 LOC

### 2. Hyperliquid Implementation

| File | LOC | Description |
|------|-----|-------------|
| `connector/hyperliquid/hyperliquid_perpetual_connector.h` | ~200 | Main connector class |
| `connector/hyperliquid/hyperliquid_perpetual_connector.cpp` | ~800 | Connector implementation |
| `connector/hyperliquid/hyperliquid_auth.h` | ~100 | EIP-712 signature generation |
| `connector/hyperliquid/hyperliquid_auth.cpp` | ~300 | Auth implementation |
| `connector/hyperliquid/hyperliquid_web_utils.h` | ~80 | Utility functions |
| `connector/hyperliquid/hyperliquid_web_utils.cpp` | ~200 | Utils implementation |
| `connector/hyperliquid/hyperliquid_order_book_data_source.cpp` | ~400 | Market data source |
| `connector/hyperliquid/hyperliquid_user_stream_data_source.cpp` | ~350 | User data source |

**Total**: ~2,430 LOC

### 3. dYdX v4 Implementation

| File | LOC | Description |
|------|-----|-------------|
| `connector/dydx_v4/dydx_v4_perpetual_connector.h` | ~200 | Main connector class |
| `connector/dydx_v4/dydx_v4_perpetual_connector.cpp` | ~850 | Connector implementation |
| `connector/dydx_v4/dydx_v4_client.h` | ~150 | gRPC client interface |
| `connector/dydx_v4/dydx_v4_client.cpp` | ~600 | gRPC client implementation |
| `connector/dydx_v4/dydx_v4_order_book_data_source.cpp` | ~400 | Market data source |
| `connector/dydx_v4/dydx_v4_user_stream_data_source.cpp` | ~300 | User data source |
| `connector/dydx_v4/private_key.cpp` | ~200 | Cosmos key derivation |
| `connector/dydx_v4/transaction.cpp` | ~300 | Transaction builder |

**Total**: ~3,000 LOC

### 4. Tests

| File | LOC | Description |
|------|-----|-------------|
| `tests/unit/connector/test_*.cpp` | ~1,500 | Unit tests for core components |
| `tests/unit/hyperliquid/test_*.cpp` | ~800 | Hyperliquid-specific tests |
| `tests/unit/dydx_v4/test_*.cpp` | ~800 | dYdX-specific tests |
| `tests/integration/test_*.cpp` | ~1,200 | Integration tests |
| `tests/performance/bench_*.cpp` | ~600 | Performance benchmarks |

**Total**: ~4,900 LOC

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

## Line Count Estimates

| Component | Header LOC | Source LOC | Test LOC | Total |
|-----------|------------|------------|----------|-------|
| Core Framework | 1,150 | 1,500 | 1,500 | 4,150 |
| Hyperliquid | 580 | 1,850 | 800 | 3,230 |
| dYdX v4 | 650 | 2,350 | 800 | 3,800 |
| Facades | 200 | 400 | 300 | 900 |
| Integration Tests | - | - | 1,200 | 1,200 |
| Benchmarks | - | - | 600 | 600 |
| **TOTAL NEW CODE** | **2,580** | **6,100** | **5,200** | **13,880** |

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

- **New Code**: ~13,880 LOC
- **Modified Existing**: ~390 LOC
- **Deprecated**: ~2,000 LOC (removed after migration)
- **Net Addition**: ~12,270 LOC

The refactoring adds comprehensive connector framework while maintaining backward compatibility through adapter facades. All new code follows Hummingbot's proven architecture patterns.

---

**End of Refactoring Plan Documentation**

Ready to begin Phase 1 implementation? 🚀
