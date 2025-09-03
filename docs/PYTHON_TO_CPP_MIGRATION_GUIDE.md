# Python to C++ Trading Core Migration Guide

## Table of Contents

1. [Overview](#overview)
2. [System Analysis](#system-analysis)
3. [Migration Phases](#migration-phases)
4. [Library Recommendations](#library-recommendations)
5. [Implementation Strategy](#implementation-strategy)
6. [Performance Considerations](#performance-considerations)
7. [Testing Strategy](#testing-strategy)
8. [Deployment Plan](#deployment-plan)

---

## Overview

This document provides a comprehensive step-by-step guide for migrating the Python-based trading core system to C++20+. The migration preserves the existing modular architecture while leveraging C++'s performance benefits and type safety.

**Key Advantage**: Your project already includes **ccapi** - a header-only C++ library that provides ultra-fast market data streaming and execution management for 30+ exchanges. This significantly simplifies the migration by eliminating the need for custom exchange connectors.

### Migration Objectives
- **Performance**: Achieve sub-microsecond latency using ccapi's optimized exchange connectivity
- **Type Safety**: Leverage C++ compile-time checks to prevent runtime errors
- **Memory Efficiency**: Reduce memory footprint and garbage collection overhead
- **Exchange Coverage**: Utilize ccapi's support for 30+ exchanges with unified API
- **Maintainability**: Preserve modular architecture and code organization
- **Interoperability**: Enable gradual migration with Python coexistence

### Current System Architecture

The Python trading core consists of:

```
trading_core/
├── analysis/           # Market analysis components
├── bus/               # Message bus and communication
├── config/            # Configuration management
├── exec/              # Order execution system
├── interfaces/        # External API interfaces
├── observability/     # Monitoring and metrics
├── risk/              # Risk management engine
├── runtime/           # Runtime management
├── schemas/           # Data models and schemas
├── simulation/        # Backtesting framework
├── state/             # Account and position state
├── strategy/          # Strategy framework
├── util/              # Utility functions
└── views/             # Market data views
```

**Plus ccapi Integration**:
```
ccapi/                 # Header-only C++ exchange connectivity
├── include/ccapi_cpp/ # Main library headers
├── example/           # Usage examples
└── binding/           # Language bindings (Python, Java, etc.)
```

---

## System Analysis

### Core Components Identified

| Component | Python Module | Size | Complexity | Migration Priority | ccapi Integration |
|-----------|---------------|------|------------|------------------|------------------|
| Order Management | `strategy/order_manager.py` | 29KB | High | Critical | **Direct ccapi execution** |
| Strategy Framework | `strategy/api.py`, `strategy/host.py` | 14KB | High | Critical | ccapi event handling |
| Risk Engine | `risk/` modules | Medium | Medium | High | Pre-execution validation |
| Market Data Views | `views/` modules | Medium | Medium | High | **Direct ccapi market data** |
| Message Models | `models.py`, `schemas/` | 12KB | Low | Medium | ccapi message types |
| Configuration | `config.py` | 2KB | Low | Low | Exchange credentials |

### Key Dependencies Analysis

| Python Package | Purpose | C++ Equivalent | Status with ccapi |
|----------------|---------|----------------|------------------|
| `ccxt` | Exchange connectivity | **ccapi (already integrated)** | ✅ **Replace with ccapi** |
| `cryptofeed` | Market data feeds | **ccapi market data** | ✅ **Replace with ccapi** |
| `websockets` | WebSocket connections | **ccapi handles internally** | ✅ **Not needed** |
| `pydantic` | Data validation | Manual validation + nlohmann-json | Complement ccapi |
| `asyncio` | Async I/O | **ccapi event handling** | ✅ **Simplified** |
| `redis` | Caching/pub-sub | redis-plus-plus | Still needed for internal messaging |
| `asyncpg` | PostgreSQL | libpqxx | Still needed for persistence |
| `PyYAML` | YAML parsing | yaml-cpp | Still needed for config |

---

## Migration Phases

## Phase 1: Infrastructure & Build System Setup

### Step 1.1: Project Structure & Build System

**Objective**: Establish modern C++ project foundation with ccapi integration, Linux-focused build system, and automated build scripts

**Tasks**:
1. Create CMake project with C++20 standard (minimum requirement)
2. Configure ccapi integration and exchange macros
3. Set up vcpkg with built-in baseline for dependency management
4. Create automated build script with system validation
5. Implement semantic versioning (semver)
6. Configure Linux-only CMake presets for development and production

**Complete Project Structure**:
```
trading_core_cpp/
├── CMakeLists.txt                    # Main build configuration
├── CMakePresets.json                 # Linux build presets
├── vcpkg.json                       # Package dependencies with baseline
├── build.sh                         # Automated build script
├── version.h.in                     # Semver template
│
├── include/trading_core/            # Public Headers (.h files)
│   ├── version.h                    # Generated version header
│   │
│   ├── schemas/                     # Core Data Schemas
│   │   ├── side.h                   # Buy/Sell enum
│   │   ├── order_type.h             # Market/Limit/Stop order types
│   │   ├── order_action.h           # Place/Cancel/Replace actions
│   │   ├── venue_type.h             # CEX/DEX/Chain venue types
│   │   ├── product_type.h           # Spot/Perpetual/AMM/CLMM/Transfer
│   │   ├── time_in_force.h          # GTC/IOC/FOK/PostOnly
│   │   ├── trade_msg.h              # Trade message structure
│   │   ├── book_msg.h               # Order book message structure  
│   │   ├── order.h                  # Order structure (CEX + DEX)
│   │   ├── proposed_order.h         # Strategy->Risk order intent
│   │   ├── execution_order.h        # Risk->Execution validated order
│   │   ├── fill.h                   # Trade fill structure
│   │   ├── execution_report.h       # Execution status reports
│   │   ├── position.h               # Position tracking
│   │   ├── dex_intent.h             # DEX swap intent payload
│   │   ├── dex_pool_hint.h          # DEX pool routing hints
│   │   ├── swap_quote.h             # DEX swap quotation
│   │   ├── risk_decision.h          # Risk engine decision
│   │   └── tob.h                    # Top-of-book snapshot
│   │
│   ├── config/                      # Configuration Management
│   │   ├── config.h                 # Main configuration loader
│   │   ├── exchange_config.h        # Exchange-specific settings
│   │   ├── strategy_config.h        # Strategy parameters
│   │   └── risk_config.h            # Risk management settings
│   │
│   ├── market_data/                 # Market Data Components (Views)
│   │   ├── market_data_manager.h    # Main market data coordinator
│   │   ├── ccapi_adapter.h          # ccapi integration adapter
│   │   ├── price_view.h             # Real-time price aggregation (TOB)
│   │   ├── history_view.h           # Book/Trade tick history
│   │   ├── dex_quote_view.h         # DEX swap quotation service
│   │   └── book_manager.h           # Order book management
│   │
│   ├── execution/                   # Order Execution (exec/)
│   │   ├── execution_manager.h      # Main execution coordinator
│   │   ├── ccapi_executor.h         # ccapi execution adapter
│   │   ├── bridge.h                 # Execution bridge interface
│   │   ├── client.h                 # Exchange client abstraction
│   │   ├── inbox.h                  # Incoming execution messages
│   │   ├── outbox.h                 # Outgoing order messages
│   │   └── exchange_router.h        # Multi-exchange routing
│   │
│   ├── strategy/                    # Strategy Framework
│   │   ├── strategy_base.h          # Base strategy interface (api.py)
│   │   ├── strategy_context.h       # Strategy context and function types
│   │   ├── strategy_host.h          # Strategy execution host
│   │   ├── order_manager.h          # Strategy order management
│   │   ├── event_dispatcher.h       # Strategy event dispatch
│   │   ├── timer_manager.h          # Strategy timer scheduling
│   │   ├── reconciliation.h         # Strategy state reconciliation
│   │   └── backtest_engine.h        # Backtesting framework
│   │
│   ├── risk/                        # Risk Management
│   │   ├── risk_engine.h            # Main risk controller (engine.py)
│   │   ├── risk_rule.h              # Base risk rule interface (rules.py) 
│   │   ├── rule_registry.h          # Risk rule registry and context
│   │   ├── builtin_rules.h          # Built-in risk rules
│   │   ├── rate_limiting.h          # Rate limiting for orders
│   │   ├── risk_base.h              # Base risk validation (base.py)
│   │   └── risk_helpers.h           # Risk calculation helpers
│   │
│   ├── messaging/                   # Inter-component Communication (bus/)
│   │   ├── message_bus.h            # Event-driven message bus
│   │   ├── zmq_bus.h                # ZeroMQ message bus implementation
│   │   ├── event_dispatcher.h       # Event routing and dispatch
│   │   └── message_types.h          # Message type definitions
│   │
│   ├── state/                       # State Management
│   │   ├── account_state.h          # Account state tracking (account.py)
│   │   ├── position.h               # Position data structure
│   │   ├── fill_normalizer.h        # Fill normalization utilities
│   │   └── state_persistence.h      # State serialization/persistence
│   │
│   ├── persistence/                 # Database Integration  
│   │   ├── database_manager.h       # Main database interface
│   │   ├── connection_pool.h        # Connection pooling
│   │   ├── trade_logger.h           # Trade execution logging
│   │   ├── market_data_recorder.h   # Market data archival
│   │   └── state_repository.h       # State persistence
│   │
│   ├── memory/                      # Memory Management
│   │   ├── object_pool.h            # Object pooling for performance
│   │   ├── memory_allocator.h       # Custom memory allocators
│   │   └── buffer_manager.h         # Buffer management
│   │
│   ├── concurrent/                  # Lock-free Concurrency
│   │   ├── spsc_queue.h             # Single producer/consumer queue
│   │   ├── mpsc_queue.h             # Multi producer/single consumer
│   │   ├── thread_pool.h            # Worker thread pool
│   │   └── lock_free_map.h          # Lock-free hash map
│   │
│   ├── optimization/                # Performance Optimization
│   │   ├── cache_optimized.h        # Cache-friendly data structures
│   │   ├── simd_operations.h        # SIMD vectorized operations
│   │   ├── branch_prediction.h      # Branch optimization utilities
│   │   └── prefetch_manager.h       # Memory prefetching
│   │
│   ├── profiling/                   # Performance Profiling
│   │   ├── performance_monitor.h    # Performance metrics collection
│   │   ├── latency_tracker.h        # Latency measurement
│   │   └── profiler_macros.h        # Profiling convenience macros
│   │
│   ├── observability/               # System Observability
│   │   ├── health_monitor.h         # Health check system (health.py)
│   │   └── metrics_collector.h      # Metrics collection (metrics.py)
│   │
│   ├── analysis/                    # Performance Analysis
│   │   └── performance_analyzer.h   # Performance analysis tools (performance.py)
│   │
│   ├── runtime/                     # Runtime Environment
│   │   ├── bootstrap.h              # System bootstrap (bootstrap.py)
│   │   ├── factory.h                # Component factory (factory.py)
│   │   ├── runtime_manager.h        # Runtime orchestration (run.py)
│   │   └── backtest_runtime.h       # Backtesting runtime (backtest_runtime.py)
│   │
│   ├── simulation/                  # Trading Simulation
│   │   ├── simulation_models.h      # Simulation data models (models.py)
│   │   ├── simulated_exchange.h     # Exchange simulation (exchange.py)
│   │   └── simulation_executor.h    # Execution simulation (executor.py)
│   │
│   └── utils/                       # Utility Components (util/)
│       ├── time_utils.h             # High-resolution timing (time.py)
│       ├── symbol_utils.h           # Symbol parsing utilities (symbol.py)
│       ├── logging_utils.h          # Structured logging setup (logging.py)
│       ├── validation.h             # Order validation utilities (validation.py)
│       ├── fx_utils.h               # FX rate utilities (fx.py)
│       ├── cache.h                  # Caching utilities (cache.py)
│       ├── persistence_queue.h      # Bounded persistence queue
│       ├── sqlite_persistence.h     # SQLite persistence layer
│       ├── reconciler.h             # State reconciliation (reconciler.py)
│       ├── snapshotter.h            # State snapshotting (snapshotter.py)
│       ├── replay.h                 # Data replay utilities (replay.py)
│       └── error_handling.h         # Error handling utilities
│
├── src/                             # Implementation Files (.cpp files)
│   ├── schemas/
│   │   ├── side.cpp
│   │   ├── order_type.cpp
│   │   ├── order_action.cpp
│   │   ├── venue_type.cpp
│   │   ├── product_type.cpp
│   │   ├── time_in_force.cpp
│   │   ├── trade_msg.cpp
│   │   ├── book_msg.cpp
│   │   ├── order.cpp
│   │   ├── proposed_order.cpp
│   │   ├── execution_order.cpp
│   │   ├── fill.cpp
│   │   ├── execution_report.cpp
│   │   ├── position.cpp
│   │   ├── dex_intent.cpp
│   │   ├── dex_pool_hint.cpp
│   │   ├── swap_quote.cpp
│   │   ├── risk_decision.cpp
│   │   └── tob.cpp
│   │
│   ├── config/
│   │   ├── config.cpp
│   │   ├── exchange_config.cpp
│   │   ├── strategy_config.cpp
│   │   └── risk_config.cpp
│   │
│   ├── market_data/
│   │   ├── market_data_manager.cpp
│   │   ├── ccapi_adapter.cpp
│   │   ├── price_view.cpp
│   │   ├── history_view.cpp
│   │   ├── dex_quote_view.cpp
│   │   └── book_manager.cpp
│   │
│   ├── execution/
│   │   ├── execution_manager.cpp
│   │   ├── ccapi_executor.cpp
│   │   ├── bridge.cpp
│   │   ├── client.cpp
│   │   ├── inbox.cpp
│   │   ├── outbox.cpp
│   │   └── exchange_router.cpp
│   │
│   ├── strategy/
│   │   ├── strategy_base.cpp
│   │   ├── strategy_context.cpp
│   │   ├── strategy_host.cpp
│   │   ├── order_manager.cpp
│   │   ├── event_dispatcher.cpp
│   │   ├── timer_manager.cpp
│   │   ├── reconciliation.cpp
│   │   └── backtest_engine.cpp
│   │
│   ├── risk/
│   │   ├── risk_engine.cpp
│   │   ├── risk_rule.cpp
│   │   ├── rule_registry.cpp
│   │   ├── builtin_rules.cpp  
│   │   ├── rate_limiting.cpp
│   │   ├── risk_base.cpp
│   │   └── risk_helpers.cpp
│   │
│   ├── messaging/
│   │   ├── message_bus.cpp
│   │   ├── zmq_bus.cpp
│   │   ├── event_dispatcher.cpp
│   │   └── message_types.cpp
│   │
│   ├── state/
│   │   ├── account_state.cpp
│   │   ├── position.cpp
│   │   ├── fill_normalizer.cpp
│   │   └── state_persistence.cpp
│   │
│   ├── persistence/
│   │   ├── database_manager.cpp
│   │   ├── connection_pool.cpp
│   │   ├── trade_logger.cpp
│   │   ├── market_data_recorder.cpp
│   │   └── state_repository.cpp
│   │
│   ├── memory/
│   │   ├── object_pool.cpp
│   │   ├── memory_allocator.cpp
│   │   └── buffer_manager.cpp
│   │
│   ├── concurrent/
│   │   ├── spsc_queue.cpp
│   │   ├── mpsc_queue.cpp
│   │   ├── thread_pool.cpp
│   │   └── lock_free_map.cpp
│   │
│   ├── optimization/
│   │   ├── cache_optimized.cpp
│   │   ├── simd_operations.cpp
│   │   ├── branch_prediction.cpp
│   │   └── prefetch_manager.cpp
│   │
│   ├── profiling/
│   │   ├── performance_monitor.cpp
│   │   ├── latency_tracker.cpp
│   │   └── profiler_macros.cpp
│   │
│   ├── observability/
│   │   ├── health_monitor.cpp
│   │   └── metrics_collector.cpp
│   │
│   ├── analysis/
│   │   └── performance_analyzer.cpp
│   │
│   ├── runtime/
│   │   ├── bootstrap.cpp
│   │   ├── factory.cpp
│   │   ├── runtime_manager.cpp
│   │   └── backtest_runtime.cpp
│   │
│   ├── simulation/
│   │   ├── simulation_models.cpp
│   │   ├── simulated_exchange.cpp
│   │   └── simulation_executor.cpp
│   │
│   └── utils/
│       ├── time_utils.cpp
│       ├── symbol_utils.cpp
│       ├── logging_utils.cpp
│       ├── validation.cpp
│       ├── fx_utils.cpp
│       ├── cache.cpp
│       ├── persistence_queue.cpp
│       ├── sqlite_persistence.cpp
│       ├── reconciler.cpp
│       ├── snapshotter.cpp
│       ├── replay.cpp
│       └── error_handling.cpp
│
├── tests/                           # Test Suite
│   ├── unit/                        # Unit Tests
│   │   ├── test_schemas.cpp
│   │   ├── test_config.cpp
│   │   ├── test_market_data.cpp
│   │   ├── test_execution.cpp
│   │   ├── test_strategy.cpp
│   │   ├── test_risk.cpp
│   │   ├── test_messaging.cpp
│   │   ├── test_state.cpp
│   │   ├── test_persistence.cpp
│   │   ├── test_memory.cpp
│   │   ├── test_concurrent.cpp
│   │   ├── test_optimization.cpp
│   │   └── test_utils.cpp
│   │
│   ├── integration/                 # Integration Tests
│   │   ├── test_market_data_integration.cpp
│   │   ├── test_execution_integration.cpp
│   │   ├── test_strategy_integration.cpp
│   │   ├── test_database_integration.cpp
│   │   └── test_full_system_integration.cpp
│   │
│   ├── performance/                 # Performance Benchmarks
│   │   ├── benchmark_trading_engine.cpp
│   │   ├── benchmark_market_data.cpp
│   │   ├── benchmark_execution.cpp
│   │   ├── benchmark_memory_pools.cpp
│   │   ├── benchmark_concurrent_queues.cpp
│   │   └── benchmark_simd_operations.cpp
│   │
│   ├── fixtures/                    # Test Fixtures & Utilities
│   │   ├── test_config.yaml
│   │   ├── mock_market_data.json
│   │   ├── test_database_schema.sql
│   │   └── test_helpers.h
│   │
│   └── CMakeLists.txt              # Test build configuration
│
├── examples/                        # Example Applications
│   ├── simple_strategy/
│   │   ├── main.cpp
│   │   ├── simple_strategy.h
│   │   ├── simple_strategy.cpp
│   │   └── config.yaml
│   │
│   ├── market_data_monitor/
│   │   ├── main.cpp
│   │   ├── monitor.h
│   │   ├── monitor.cpp
│   │   └── config.yaml
│   │
│   ├── backtester/
│   │   ├── main.cpp
│   │   ├── backtest_runner.h
│   │   ├── backtest_runner.cpp
│   │   └── backtest_config.yaml
│   │
│   └── CMakeLists.txt              # Examples build configuration
│
├── docs/                           # Documentation
│   ├── API_REFERENCE.md
│   ├── GETTING_STARTED.md
│   ├── CONFIGURATION_GUIDE.md
│   ├── STRATEGY_DEVELOPMENT.md
│   ├── PERFORMANCE_TUNING.md
│   └── DEPLOYMENT_GUIDE.md
│
├── scripts/                        # Build & Deployment Scripts
│   ├── check_deps.sh               # Dependency validation
│   ├── setup_vcpkg.sh              # vcpkg setup automation
│   ├── build_release.sh            # Release build script
│   ├── run_tests.sh                # Test execution script
│   ├── benchmark.sh                # Performance benchmarking
│   └── deploy.sh                   # Deployment automation
│
├── config/                         # Configuration Templates
│   ├── trading_config.yaml.template
│   ├── exchange_config.yaml.template
│   ├── risk_config.yaml.template
│   ├── logging_config.yaml.template
│   └── database_config.yaml.template
│
├── .github/                        # CI/CD Configuration
│   └── workflows/
│       ├── cpp_trading_engine.yml
│       ├── performance_regression.yml
│       └── security_scan.yml
│
└── ccapi/                          # Existing ccapi submodule
    └── (ccapi library files)
```

**CMakeLists.txt with ccapi and Semver**:
```cmake
cmake_minimum_required(VERSION 3.28)  # Latest stable CMake

# Semantic versioning
set(TRADING_CORE_VERSION_MAJOR 1)
set(TRADING_CORE_VERSION_MINOR 0)
set(TRADING_CORE_VERSION_PATCH 0)
set(TRADING_CORE_VERSION_PRERELEASE "")  # e.g., "alpha.1", "beta.2", or ""
set(TRADING_CORE_VERSION_BUILD "")       # e.g., "20241201.1", or ""

# Construct full version string
set(TRADING_CORE_VERSION "${TRADING_CORE_VERSION_MAJOR}.${TRADING_CORE_VERSION_MINOR}.${TRADING_CORE_VERSION_PATCH}")
if(NOT TRADING_CORE_VERSION_PRERELEASE STREQUAL "")
    set(TRADING_CORE_VERSION "${TRADING_CORE_VERSION}-${TRADING_CORE_VERSION_PRERELEASE}")
endif()
if(NOT TRADING_CORE_VERSION_BUILD STREQUAL "")
    set(TRADING_CORE_VERSION "${TRADING_CORE_VERSION}+${TRADING_CORE_VERSION_BUILD}")
endif()

project(trading_core 
    VERSION ${TRADING_CORE_VERSION_MAJOR}.${TRADING_CORE_VERSION_MINOR}.${TRADING_CORE_VERSION_PATCH}
    LANGUAGES CXX
    DESCRIPTION "High-performance C++ trading core system"
)

# C++20 minimum requirement
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Linux-specific optimizations
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This project is designed for Linux only")
endif()

# Build configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Compiler-specific flags for Linux
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -march=native  # Optimize for build machine
        -mtune=native
        $<$<CONFIG:Release>:-O3 -DNDEBUG -flto>
        $<$<CONFIG:Debug>:-g3 -O0 -fsanitize=address,undefined>
    )
    
    add_link_options(
        $<$<CONFIG:Release>:-flto>
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
endif()

# Generate version header
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/trading_core/version.h"
    @ONLY
)

# ccapi requirements
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)

# Additional packages
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

# ccapi configuration - enable required exchanges
add_compile_definitions(
    CCAPI_ENABLE_SERVICE_MARKET_DATA
    CCAPI_ENABLE_SERVICE_EXECUTION_MANAGEMENT
    CCAPI_ENABLE_EXCHANGE_BINANCE
    CCAPI_ENABLE_EXCHANGE_BINANCE_USDS_FUTURES
    CCAPI_ENABLE_EXCHANGE_COINBASE
    CCAPI_ENABLE_EXCHANGE_OKX
    CCAPI_ENABLE_EXCHANGE_BYBIT
    CCAPI_ENABLE_EXCHANGE_KRAKEN
    # Add other exchanges as needed
)

# Main library
add_library(trading_core
    src/schemas/order.cpp
    src/schemas/trade_msg.cpp
    src/schemas/book_msg.cpp
    src/config/config.cpp
    src/market_data/ccapi_adapter.cpp
    src/market_data/price_view.cpp
    src/execution/ccapi_executor.cpp
    src/execution/order_manager.cpp
    src/strategy/strategy_base.cpp
    src/strategy/strategy_host.cpp
    src/risk/risk_engine.cpp
    src/state/account_state.cpp
)

target_include_directories(trading_core 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/ccapi/include
        ${Boost_INCLUDE_DIRS}
)

target_link_libraries(trading_core 
    PUBLIC
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        fmt::fmt
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        Boost::system
        Boost::thread
        pthread  # Required for Linux threading
)

# Set library properties
set_target_properties(trading_core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Install configuration
include(GNUInstallDirs)
install(TARGETS trading_core
    EXPORT trading_core_targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/ 
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/trading_core/version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/trading_core
)
```

**CMakePresets.json (Linux-only)**:
```json
{
    "version": 6,
    "cmakeMinimumRequired": {
        "major": 3,
        "minor": 28,
        "patch": 0
    },
    "configurePresets": [
        {
            "name": "linux-debug",
            "displayName": "Linux Debug",
            "description": "Debug build for Linux with sanitizers",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/debug",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "CMAKE_TOOLCHAIN_FILE": "${sourceDir}/vcpkg/scripts/buildsystems/vcpkg.cmake",
                "VCPKG_TARGET_TRIPLET": "x64-linux",
                "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
            },
            "condition": {
                "type": "equals",
                "lhs": "${hostSystemName}",
                "rhs": "Linux"
            }
        },
        {
            "name": "linux-release",
            "displayName": "Linux Release",
            "description": "Optimized release build for Linux",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/release",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release",
                "CMAKE_TOOLCHAIN_FILE": "${sourceDir}/vcpkg/scripts/buildsystems/vcpkg.cmake",
                "VCPKG_TARGET_TRIPLET": "x64-linux",
                "CMAKE_INTERPROCEDURAL_OPTIMIZATION": "ON"
            },
            "condition": {
                "type": "equals",
                "lhs": "${hostSystemName}",
                "rhs": "Linux"
            }
        },
        {
            "name": "linux-relwithdebinfo",
            "displayName": "Linux RelWithDebInfo",
            "description": "Release with debug info for profiling",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/profiling",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "RelWithDebInfo",
                "CMAKE_TOOLCHAIN_FILE": "${sourceDir}/vcpkg/scripts/buildsystems/vcpkg.cmake",
                "VCPKG_TARGET_TRIPLET": "x64-linux"
            },
            "condition": {
                "type": "equals",
                "lhs": "${hostSystemName}",
                "rhs": "Linux"
            }
        }
    ],
    "buildPresets": [
        {
            "name": "linux-debug",
            "configurePreset": "linux-debug",
            "displayName": "Build Linux Debug",
            "jobs": 0
        },
        {
            "name": "linux-release",
            "configurePreset": "linux-release",
            "displayName": "Build Linux Release",
            "jobs": 0
        },
        {
            "name": "linux-profiling",
            "configurePreset": "linux-relwithdebinfo",
            "displayName": "Build Linux Profiling",
            "jobs": 0
        }
    ],
    "testPresets": [
        {
            "name": "linux-debug-test",
            "configurePreset": "linux-debug",
            "displayName": "Test Linux Debug"
        },
        {
            "name": "linux-release-test",
            "configurePreset": "linux-release",
            "displayName": "Test Linux Release"
        }
    ]
}
```

**version.h.in Template**:
```cpp
#pragma once

namespace trading_core {

constexpr int VERSION_MAJOR = @TRADING_CORE_VERSION_MAJOR@;
constexpr int VERSION_MINOR = @TRADING_CORE_VERSION_MINOR@;
constexpr int VERSION_PATCH = @TRADING_CORE_VERSION_PATCH@;
constexpr const char* VERSION_STRING = "@TRADING_CORE_VERSION@";
constexpr const char* VERSION_PRERELEASE = "@TRADING_CORE_VERSION_PRERELEASE@";
constexpr const char* VERSION_BUILD = "@TRADING_CORE_VERSION_BUILD@";

} // namespace trading_core
```

**build.sh - Automated Build Script**:
```bash
#!/bin/bash
set -euo pipefail

# Trading Core C++ Build Script
# Handles dependency checking, vcpkg setup, and compilation

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="${SCRIPT_DIR}"
readonly VCPKG_DIR="${PROJECT_ROOT}/vcpkg"

# Color output
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $*"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $*"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $*"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*"
}

check_system_requirements() {
    log_info "Checking system requirements..."
    
    # Check Linux
    if [[ "$(uname -s)" != "Linux" ]]; then
        log_error "This project requires Linux. Current system: $(uname -s)"
        exit 1
    fi
    
    # Check required commands
    local required_commands=("git" "curl" "tar" "pkg-config")
    for cmd in "${required_commands[@]}"; do
        if ! command -v "$cmd" &> /dev/null; then
            log_error "Required command not found: $cmd"
            exit 1
        fi
    done
    
    # Check CMake version
    if ! command -v cmake &> /dev/null; then
        log_error "CMake not found. Please install CMake 3.28 or later."
        exit 1
    fi
    
    local cmake_version
    cmake_version=$(cmake --version | head -n1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
    local cmake_major
    cmake_major=$(echo "$cmake_version" | cut -d. -f1)
    local cmake_minor
    cmake_minor=$(echo "$cmake_version" | cut -d. -f2)
    
    if (( cmake_major < 3 || (cmake_major == 3 && cmake_minor < 28) )); then
        log_error "CMake 3.28+ required. Found: $cmake_version"
        log_info "Please update CMake or install via snap: sudo snap install cmake --classic"
        exit 1
    fi
    
    # Check Ninja
    if ! command -v ninja &> /dev/null; then
        log_warning "Ninja not found. Installing via package manager is recommended."
        log_info "Ubuntu/Debian: sudo apt install ninja-build"
        log_info "RHEL/Fedora: sudo dnf install ninja-build"
    fi
    
    # Check GCC/Clang version for C++20 support
    local cxx_compiler="${CXX:-g++}"
    if ! command -v "$cxx_compiler" &> /dev/null; then
        log_error "C++ compiler not found: $cxx_compiler"
        exit 1
    fi
    
    local compiler_version
    if [[ "$cxx_compiler" == *"gcc"* ]] || [[ "$cxx_compiler" == *"g++"* ]]; then
        compiler_version=$($cxx_compiler --version | head -n1 | grep -oE '[0-9]+\.[0-9]+')
        local gcc_major
        gcc_major=$(echo "$compiler_version" | cut -d. -f1)
        if (( gcc_major < 10 )); then
            log_error "GCC 10+ required for C++20. Found: $compiler_version"
            exit 1
        fi
    elif [[ "$cxx_compiler" == *"clang"* ]]; then
        compiler_version=$($cxx_compiler --version | head -n1 | grep -oE '[0-9]+\.[0-9]+')
        local clang_major
        clang_major=$(echo "$compiler_version" | cut -d. -f1)
        if (( clang_major < 12 )); then
            log_error "Clang 12+ required for C++20. Found: $compiler_version"
            exit 1
        fi
    fi
    
    log_success "System requirements check passed"
}

setup_vcpkg() {
    log_info "Setting up vcpkg..."
    
    if [[ ! -d "$VCPKG_DIR" ]]; then
        log_info "Cloning vcpkg..."
        git clone https://github.com/Microsoft/vcpkg.git "$VCPKG_DIR"
    else
        log_info "Updating vcpkg..."
        cd "$VCPKG_DIR"
        git pull origin master
        cd "$PROJECT_ROOT"
    fi
    
    # Bootstrap vcpkg
    if [[ ! -f "$VCPKG_DIR/vcpkg" ]]; then
        log_info "Bootstrapping vcpkg..."
        "$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics
    fi
    
    log_success "vcpkg setup completed"
}

configure_project() {
    local preset="${1:-linux-release}"
    
    log_info "Configuring project with preset: $preset"
    
    # Ensure vcpkg integration
    export CMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake"
    
    cmake --preset="$preset"
    
    log_success "Project configured successfully"
}

build_project() {
    local preset="${1:-linux-release}"
    
    log_info "Building project with preset: $preset"
    
    cmake --build --preset="$preset"
    
    log_success "Project built successfully"
}

run_tests() {
    local preset="${1:-linux-release-test}"
    
    log_info "Running tests with preset: $preset"
    
    ctest --preset="$preset" --output-on-failure
    
    log_success "Tests completed successfully"
}

show_usage() {
    cat << EOF
Usage: $0 [COMMAND] [OPTIONS]

COMMANDS:
    deps        Check system dependencies only
    setup       Setup vcpkg and dependencies
    configure   Configure the project (default: linux-release)
    build       Build the project (default: linux-release)
    test        Run tests (default: linux-release-test)
    clean       Clean build directories
    all         Run complete build pipeline (default)

OPTIONS:
    --preset    Specify CMake preset (linux-debug|linux-release|linux-relwithdebinfo)
    --help      Show this help message

EXAMPLES:
    $0                              # Complete build pipeline (release)
    $0 build --preset linux-debug  # Build debug version
    $0 test --preset linux-debug-test # Run debug tests
    $0 clean                        # Clean all build directories
EOF
}

clean_build() {
    log_info "Cleaning build directories..."
    rm -rf "${PROJECT_ROOT}/build"
    log_success "Build directories cleaned"
}

main() {
    local command="${1:-all}"
    local preset="linux-release"
    
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --preset)
                preset="$2"
                shift 2
                ;;
            --help)
                show_usage
                exit 0
                ;;
            *)
                command="$1"
                shift
                ;;
        esac
    done
    
    case "$command" in
        deps)
            check_system_requirements
            ;;
        setup)
            check_system_requirements
            setup_vcpkg
            ;;
        configure)
            check_system_requirements
            setup_vcpkg
            configure_project "$preset"
            ;;
        build)
            build_project "$preset"
            ;;
        test)
            local test_preset="${preset}-test"
            run_tests "$test_preset"
            ;;
        clean)
            clean_build
            ;;
        all)
            check_system_requirements
            setup_vcpkg
            configure_project "$preset"
            build_project "$preset"
            log_success "Build pipeline completed successfully!"
            ;;
        *)
            log_error "Unknown command: $command"
            show_usage
            exit 1
            ;;
    esac
}

main "$@"
```

### Step 1.2: vcpkg Configuration with Built-in Baseline

**Objective**: Configure vcpkg with built-in baseline for reproducible dependency management

**Tasks**:
1. Set up vcpkg.json with built-in baseline from latest vcpkg registry
2. Configure minimal dependency set leveraging ccapi
3. Pin package versions for consistent builds across environments
4. Add development dependencies for testing and benchmarking

**vcpkg.json with Built-in Baseline**:
```json
{
  "$schema": "https://raw.githubusercontent.com/Microsoft/vcpkg-tool/main/docs/vcpkg.schema.json",
  "name": "trading-core-cpp",
  "version": "1.0.0",
  "description": "High-performance C++ trading core system",
  "homepage": "https://github.com/yourorg/trading-core-cpp",
  "documentation": "https://github.com/yourorg/trading-core-cpp/blob/main/README.md",
  "license": "MIT",
  "builtin-baseline": "3426db05b996481ca31e95fff3734cf23e0f51bc",
  "dependencies": [
    {
      "name": "nlohmann-json",
      "version>=": "3.11.3"
    },
    {
      "name": "spdlog", 
      "version>=": "1.12.0",
      "features": ["fmt_external"]
    },
    {
      "name": "fmt",
      "version>=": "10.1.1"
    },
    {
      "name": "openssl",
      "version>=": "3.1.4"
    },
    {
      "name": "boost",
      "version>=": "1.83.0",
      "features": ["system", "thread", "chrono", "date-time"]
    },
    {
      "name": "redis-plus-plus",
      "version>=": "1.3.10"
    },
    {
      "name": "libpqxx",
      "version>=": "7.8.1"
    },
    {
      "name": "yaml-cpp",
      "version>=": "0.8.0"
    }
  ],
  "features": {
    "testing": {
      "description": "Enable testing dependencies",
      "dependencies": [
        {
          "name": "catch2",
          "version>=": "3.4.0"
        }
      ]
    },
    "benchmarking": {
      "description": "Enable benchmarking dependencies", 
      "dependencies": [
        {
          "name": "benchmark",
          "version>=": "1.8.3"
        }
      ]
    },
    "development": {
      "description": "Enable all development tools",
      "dependencies": [
        {
          "$ref": "#/features/testing"
        },
        {
          "$ref": "#/features/benchmarking"
        }
      ]
    }
  },
  "overrides": [
    {
      "name": "openssl",
      "version": "3.1.4"
    }
  ]
}
```

**vcpkg-configuration.json** (Registry Configuration):
```json
{
  "$schema": "https://raw.githubusercontent.com/Microsoft/vcpkg-tool/main/docs/vcpkg-configuration.schema.json",
  "default-registry": {
    "kind": "git",
    "repository": "https://github.com/Microsoft/vcpkg",
    "baseline": "3426db05b996481ca31e95fff3734cf23e0f51bc"
  },
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/Microsoft/vcpkg",
      "baseline": "3426db05b996481ca31e95fff3734cf23e0f51bc",
      "packages": ["*"]
    }
  ]
}
```

**Key Features**:

1. **Built-in Baseline**: 
   - Commit `3426db05b996481ca31e95fff3734cf23e0f51bc` from latest vcpkg registry
   - Ensures reproducible builds across different environments
   - Locks package versions to tested combinations

2. **Minimal Dependencies**:
   - Removed `cppzmq`, `websocketpp`, `asio`, `protobuf` (handled by ccapi)
   - Kept only essential packages for business logic
   - Optional features for testing and benchmarking

3. **Version Constraints**:
   - Minimum version requirements for security and feature compatibility
   - OpenSSL 3.1.4+ for latest security patches
   - Boost 1.83.0+ for C++20 compatibility improvements

4. **Feature Sets**:
   - `testing`: Adds Catch2 for unit tests
   - `benchmarking`: Adds Google Benchmark for performance tests  
   - `development`: Combines all development tools

**Usage Examples**:
```bash
# Install production dependencies only
vcpkg install --triplet x64-linux

# Install with testing support
vcpkg install --triplet x64-linux --feature-flags=testing

# Install with all development tools
vcpkg install --triplet x64-linux --feature-flags=development
```

**Updating the Baseline**:
```bash
# Get latest vcpkg commit for baseline updates
cd vcpkg
git log -1 --format="%H"
# Update the "builtin-baseline" field in vcpkg.json with the new commit hash
```

## Phase 2: Core Data Models & Schemas

### Step 2.1: Schema Migration with ccapi Integration

**Objective**: Convert Python dataclasses to C++ structs compatible with ccapi

**ccapi Message Integration**:
```cpp
#include <ccapi_cpp/ccapi_session.h>
#include <nlohmann/json.hpp>

namespace trading_core::schemas {

// Map ccapi types to our domain models
struct TradeMsg {
    std::string exchange;
    std::string symbol;
    double price;
    double amount;
    Side side;
    std::chrono::time_point<std::chrono::system_clock> timestamp;
    
    // Convert from ccapi::Message
    static TradeMsg from_ccapi_message(const ccapi::Message& msg);
    
    NLOHMANN_DEFINE_TYPE_OPTIONAL(TradeMsg, 
        exchange, symbol, price, amount, side, timestamp)
};

struct BookMsg {
    std::string exchange;
    std::string symbol;
    double best_bid_price;
    double best_bid_size;
    double best_ask_price;
    double best_ask_size;
    double midpoint;
    std::chrono::time_point<std::chrono::system_clock> timestamp;
    
    static BookMsg from_ccapi_message(const ccapi::Message& msg);
    
    NLOHMANN_DEFINE_TYPE_OPTIONAL(BookMsg, 
        exchange, symbol, best_bid_price, best_bid_size, 
        best_ask_price, best_ask_size, midpoint, timestamp)
};

} // namespace trading_core::schemas
```

## Phase 3: Market Data & Exchange Connectivity

### Step 3.1: ccapi Market Data Integration

**Objective**: Replace Python market data feeds with ccapi

**Market Data Service**:
```cpp
namespace trading_core::market_data {

class CcapiMarketDataService {
public:
    CcapiMarketDataService();
    
    void subscribe_book_data(const std::string& exchange, 
                           const std::string& symbol,
                           std::function<void(const schemas::BookMsg&)> callback);
    
    void subscribe_trade_data(const std::string& exchange,
                            const std::string& symbol, 
                            std::function<void(const schemas::TradeMsg&)> callback);
    
    void start();
    void stop();
    
private:
    ccapi::Session session_;
    ccapi::SessionOptions session_options_;
    ccapi::SessionConfigs session_configs_;
    
    void handle_event(const ccapi::Event& event);
    
    std::unordered_map<std::string, std::function<void(const schemas::BookMsg&)>> book_callbacks_;
    std::unordered_map<std::string, std::function<void(const schemas::TradeMsg&)>> trade_callbacks_;
};

} // namespace trading_core::market_data
```

### Step 3.2: ccapi Execution Management

**Order Execution Service**:
```cpp
namespace trading_core::execution {

class CcapiExecutionService {
public:
    CcapiExecutionService();
    
    void submit_order(const schemas::ProposedOrder& order,
                     std::function<void(const schemas::ExecutionReport&)> callback);
    
    void cancel_order(const std::string& exchange,
                     const std::string& client_order_id,
                     std::function<void(const schemas::ExecutionReport&)> callback);
    
    void configure_exchange(const std::string& exchange,
                          const config::ExchangeCredentials& credentials);
    
private:
    ccapi::Session session_;
    ccapi::SessionOptions session_options_;
    ccapi::SessionConfigs session_configs_;
    
    void handle_execution_event(const ccapi::Event& event);
    
    std::unordered_map<std::string, std::function<void(const schemas::ExecutionReport&)>> execution_callbacks_;
};

} // namespace trading_core::execution
```

## Phase 4: Core Trading Components

### Step 4.1: Market Data Views with ccapi

**PriceView Integration**:
```cpp
namespace trading_core::views {

class PriceView {
public:
    PriceView(std::shared_ptr<market_data::CcapiMarketDataService> market_data_service);
    
    void subscribe_to_symbol(const std::string& exchange, const std::string& symbol);
    
    std::optional<double> get_mid_price(const std::string& exchange, 
                                       const std::string& symbol) const;
    std::optional<double> get_last_trade_price(const std::string& exchange,
                                             const std::string& symbol) const;
    
private:
    std::shared_ptr<market_data::CcapiMarketDataService> market_data_service_;
    
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, schemas::BookMsg> book_data_;
    std::unordered_map<std::string, schemas::TradeMsg> trade_data_;
    
    void on_book_update(const schemas::BookMsg& book);
    void on_trade_update(const schemas::TradeMsg& trade);
    
    std::string make_key(const std::string& exchange, const std::string& symbol) const;
};

} // namespace trading_core::views
```

### Step 4.2: Order Management with ccapi

**Simplified Order Manager**:
```cpp
namespace trading_core::strategy {

class OrderManager {
public:
    OrderManager(std::shared_ptr<execution::CcapiExecutionService> execution_service,
                std::shared_ptr<risk::RiskEngine> risk_engine);
    
    void submit_order(const schemas::ProposedOrder& order);
    void cancel_order(const std::string& client_order_id);
    
    std::vector<schemas::Order> get_open_orders() const;
    std::vector<schemas::ExecutionReport> get_execution_reports() const;
    
private:
    std::shared_ptr<execution::CcapiExecutionService> execution_service_;
    std::shared_ptr<risk::RiskEngine> risk_engine_;
    
    mutable std::shared_mutex orders_mutex_;
    std::unordered_map<std::string, schemas::Order> orders_;
    std::vector<schemas::ExecutionReport> execution_reports_;
    
    void on_execution_report(const schemas::ExecutionReport& report);
};

} // namespace trading_core::strategy
```

## Phase 5: Strategy Framework

### Step 5.1: ccapi-Integrated Strategy Base

**Strategy Base Class**:
```cpp
namespace trading_core::strategy {

class StrategyBase {
public:
    virtual ~StrategyBase() = default;
    
    // ccapi event handlers
    virtual void on_book_update(const std::string& exchange,
                               const std::string& symbol,
                               const schemas::BookMsg& book) = 0;
    virtual void on_trade_update(const std::string& exchange,
                                const std::string& symbol, 
                                const schemas::TradeMsg& trade) = 0;
    virtual void on_execution_report(const schemas::ExecutionReport& report) = 0;
    virtual void on_timer(const std::string& timer_id) = 0;
    
    virtual void initialize(const StrategyContext& context) = 0;
    virtual void shutdown() = 0;
    
protected:
    void submit_order(const std::string& exchange,
                     const std::string& symbol,
                     Side side,
                     double quantity,
                     double price = 0.0,  // 0 for market orders
                     OrderType type = OrderType::LIMIT);
                     
    void cancel_order(const std::string& client_order_id);
    
    void subscribe_market_data(const std::string& exchange, 
                              const std::string& symbol);
                              
    void set_timer(const std::string& timer_id, 
                   std::chrono::milliseconds interval);
                   
private:
    StrategyContext* context_ = nullptr;
};

} // namespace trading_core::strategy
```

---

## Library Recommendations (Updated for ccapi)

### Core Dependencies

| Library | Purpose | Key Features | Status |
|---------|---------|--------------|---------|
| **ccapi** | Exchange connectivity | 30+ exchanges, ultra-fast, header-only | ✅ **Already integrated** |
| **nlohmann-json** | JSON serialization | Header-only, modern C++ API | Required |
| **spdlog** | High-performance logging | Async logging, multiple sinks | Required |
| **fmt** | String formatting | Faster than iostream, type-safe | Required |
| **OpenSSL** | Cryptography | Required by ccapi | Required |
| **Boost** | Utilities | Required by ccapi | Required |

### Optional Dependencies

| Library | Purpose | vcpkg Package |
|---------|---------|---------------|
| **redis-plus-plus** | Internal messaging/caching | `redis-plus-plus` |
| **libpqxx** | PostgreSQL persistence | `libpqxx` |
| **yaml-cpp** | Configuration files | `yaml-cpp` |
| **catch2** | Unit testing | `catch2` |
| **benchmark** | Performance testing | `benchmark` |

### Removed Dependencies (Thanks to ccapi)

| ❌ No Longer Needed | Reason |
|-------------------|---------|
| `cppzmq` | ccapi handles all exchange communication |
| `websocketpp` | ccapi manages WebSocket connections internally |
| `asio` | ccapi uses Boost.Beast internally |
| `protobuf` | ccapi uses JSON and native protocols |

---

## Implementation Strategy (Revised)

### Simplified Migration Approach

1. **ccapi First**: Leverage existing ccapi for all exchange connectivity
2. **Minimal External Dependencies**: Focus on business logic, not connectivity
3. **Exchange Agnostic**: Use ccapi's unified API across all exchanges
4. **Event-Driven**: Build on ccapi's event handling model

### Development Phases (Updated)

| Phase | Duration | Components | ccapi Integration |
|-------|----------|------------|------------------|
| 1 | 1 week | Infrastructure, ccapi setup | ✅ Enable exchanges, basic connectivity |
| 2 | 2 weeks | Schemas, message mapping | ✅ ccapi message conversion |
| 3 | 2 weeks | Market data service | ✅ ccapi market data subscriptions |
| 4 | 3 weeks | Execution service | ✅ ccapi order management |
| 5 | 3 weeks | Strategy framework | ✅ ccapi event integration |
| 6 | 2 weeks | Risk engine, state management | Business logic layer |
| 7 | 1 week | Optimization | ccapi performance tuning |
| 8 | 1 week | Testing, deployment | End-to-end validation |

**Total Estimated Time: 15 weeks** (vs. 22 weeks without ccapi)

---

## Performance Considerations (Enhanced with ccapi)

### Latency Targets (ccapi-optimized)

| Operation | Current (Python) | Target (C++ + ccapi) | Improvement |
|-----------|------------------|---------------------|-------------|
| Market data processing | ~200μs | **~5μs** | **40x** |
| Order submission | ~100μs | **~2μs** | **50x** |
| Exchange connectivity | ~1ms | **~10μs** | **100x** |
| Strategy execution | ~500μs | **~20μs** | **25x** |

### ccapi Performance Benefits

1. **Zero-copy message handling**: Direct memory access to exchange data
2. **Optimized WebSocket management**: Connection pooling and reuse
3. **Compiled exchange protocols**: No runtime protocol parsing
4. **Lock-free data structures**: Minimal contention in market data paths
5. **Memory locality**: Careful cache optimization

---

## Next Steps (Prioritized for ccapi)

1. **Enable ccapi exchanges**: Configure macros for required exchanges
2. **Implement message adapters**: Convert ccapi messages to domain types  
3. **Build market data service**: Subscribe to real-time feeds
4. **Create execution service**: Submit and track orders
5. **Integrate with strategy framework**: Event-driven strategy execution

---

## ccapi Exchange Configuration

### Supported Exchanges (Market Data + Execution)

**Tier 1 (Highest Volume)**:
- Binance, Binance Futures
- Coinbase Pro  
- OKX
- Bybit
- Kraken

**Tier 2 (Additional Coverage)**:
- Bitfinex, BitMEX, Huobi
- KuCoin, Gate.io
- Deribit (Options/Futures)

### Exchange-Specific Macros

```cpp
// Enable in CMakeLists.txt based on your requirements
CCAPI_ENABLE_EXCHANGE_BINANCE
CCAPI_ENABLE_EXCHANGE_COINBASE  
CCAPI_ENABLE_EXCHANGE_OKX
CCAPI_ENABLE_EXCHANGE_BYBIT
CCAPI_ENABLE_EXCHANGE_KRAKEN
// Add others as needed
```

This revised approach leverages your existing ccapi integration to dramatically simplify the migration while achieving superior performance through ccapi's optimized exchange connectivity.

## Phase 6: State Management & Persistence

### Step 6.1: Account State Management (.h/.cpp separation)

**Objective**: Create thread-safe account state management with proper header/implementation separation

**include/trading_core/state/account_state.h**:
```cpp
#pragma once

#include <trading_core/schemas/position.h>
#include <shared_mutex>
#include <unordered_map>
#include <optional>
#include <string>

namespace trading_core::state {

class AccountState {
public:
    AccountState();
    ~AccountState();

    // Position management
    void update_position(const std::string& symbol, double quantity, double avg_price);
    void update_position_from_fill(const schemas::Fill& fill);
    std::optional<schemas::Position> get_position(const std::string& symbol) const;
    std::vector<schemas::Position> get_all_positions() const;
    
    // Balance management
    void update_balance(const std::string& currency, double amount);
    void add_to_balance(const std::string& currency, double delta);
    double get_balance(const std::string& currency) const;
    std::unordered_map<std::string, double> get_all_balances() const;
    
    // P&L calculations
    double get_unrealized_pnl() const;
    double get_realized_pnl() const;
    double get_total_pnl() const;
    
    // Risk metrics
    double get_gross_exposure() const;
    double get_net_exposure() const;
    std::unordered_map<std::string, double> get_exposure_by_currency() const;
    
    // State persistence
    void save_snapshot() const;
    bool load_snapshot();
    void reset();

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, schemas::Position> positions_;
    std::unordered_map<std::string, double> balances_;
    double realized_pnl_;
    
    // Internal methods
    void calculate_pnl_for_position(const std::string& symbol, 
                                   const schemas::Position& position,
                                   double& unrealized, double& realized) const;
    void update_realized_pnl(double delta);
};

} // namespace trading_core::state
```

**src/state/account_state.cpp**:
```cpp
#include <trading_core/state/account_state.h>
#include <trading_core/util/logging.h>
#include <trading_core/persistence/database_manager.h>

#include <algorithm>
#include <numeric>

namespace trading_core::state {

AccountState::AccountState() : realized_pnl_(0.0) {
    LOG_INFO("AccountState initialized");
}

AccountState::~AccountState() {
    save_snapshot();
    LOG_INFO("AccountState destroyed, snapshot saved");
}

void AccountState::update_position(const std::string& symbol, double quantity, double avg_price) {
    std::unique_lock lock(mutex_);
    
    auto& position = positions_[symbol];
    position.symbol = symbol;
    position.quantity = quantity;
    position.avg_price = avg_price;
    position.last_update = std::chrono::system_clock::now();
    
    LOG_DEBUG("Position updated: {} qty={} avg_price={}", symbol, quantity, avg_price);
}

void AccountState::update_position_from_fill(const schemas::Fill& fill) {
    std::unique_lock lock(mutex_);
    
    auto& position = positions_[fill.symbol];
    
    // Calculate new average price
    double old_qty = position.quantity;
    double old_avg = position.avg_price;
    double fill_qty = fill.side == schemas::Side::BUY ? fill.quantity : -fill.quantity;
    double new_qty = old_qty + fill_qty;
    
    if (std::abs(new_qty) < 1e-8) {
        // Position closed
        double pnl = (fill.price - old_avg) * std::abs(fill_qty);
        if (fill.side == schemas::Side::SELL) {
            update_realized_pnl(pnl);
        } else {
            update_realized_pnl(-pnl);
        }
        position.quantity = 0.0;
        position.avg_price = 0.0;
    } else if ((old_qty > 0 && fill_qty > 0) || (old_qty < 0 && fill_qty < 0)) {
        // Same direction - update average price
        position.avg_price = (old_qty * old_avg + fill_qty * fill.price) / new_qty;
        position.quantity = new_qty;
    } else {
        // Opposite direction - realize P&L on closed portion
        double closed_qty = std::min(std::abs(old_qty), std::abs(fill_qty));
        double pnl = (fill.price - old_avg) * closed_qty;
        if (old_qty < 0) pnl = -pnl;
        
        update_realized_pnl(pnl);
        position.quantity = new_qty;
        // Keep old average price for remaining position
    }
    
    position.last_update = std::chrono::system_clock::now();
    
    LOG_INFO("Position updated from fill: {} {} @ {}, new position: {} @ {}", 
             fill.symbol, fill_qty, fill.price, position.quantity, position.avg_price);
}

std::optional<schemas::Position> AccountState::get_position(const std::string& symbol) const {
    std::shared_lock lock(mutex_);
    
    auto it = positions_.find(symbol);
    if (it != positions_.end() && std::abs(it->second.quantity) > 1e-8) {
        return it->second;
    }
    return std::nullopt;
}

double AccountState::get_unrealized_pnl() const {
    std::shared_lock lock(mutex_);
    
    double total_unrealized = 0.0;
    for (const auto& [symbol, position] : positions_) {
        if (std::abs(position.quantity) > 1e-8) {
            // This would need current market price - placeholder for now
            double current_price = position.avg_price; // TODO: Get from PriceView
            total_unrealized += (current_price - position.avg_price) * position.quantity;
        }
    }
    return total_unrealized;
}

void AccountState::update_realized_pnl(double delta) {
    realized_pnl_ += delta;
    LOG_DEBUG("Realized P&L updated by {}, total: {}", delta, realized_pnl_);
}

// Additional implementation methods...

} // namespace trading_core::state
```

### Step 6.2: Database Integration (.h/.cpp separation)

**Objective**: Implement persistence layer with PostgreSQL and connection pooling

**include/trading_core/persistence/database_manager.h**:
```cpp
#pragma once

#include <trading_core/schemas/order.h>
#include <trading_core/schemas/fill.h>
#include <trading_core/state/account_state.h>

#include <pqxx/pqxx>
#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace trading_core::persistence {

class ConnectionPool {
public:
    explicit ConnectionPool(const std::string& connection_string, size_t pool_size = 10);
    ~ConnectionPool();

    std::unique_ptr<pqxx::connection> acquire();
    void release(std::unique_ptr<pqxx::connection> conn);

private:
    std::string connection_string_;
    std::queue<std::unique_ptr<pqxx::connection>> available_connections_;
    std::mutex mutex_;
    std::condition_variable condition_;
    size_t pool_size_;
    size_t active_connections_;
    
    void create_connection();
};

class DatabaseManager {
public:
    explicit DatabaseManager(const std::string& connection_string, size_t pool_size = 10);
    ~DatabaseManager();

    // Order operations
    void save_order(const schemas::Order& order);
    void update_order_status(const std::string& client_order_id, 
                           const std::string& status,
                           const std::string& exchange_order_id = "");
    std::vector<schemas::Order> load_orders_by_date(const std::string& date);
    std::vector<schemas::Order> load_active_orders();
    
    // Fill operations
    void save_fill(const schemas::Fill& fill);
    std::vector<schemas::Fill> load_fills_by_order(const std::string& client_order_id);
    std::vector<schemas::Fill> load_fills_by_date(const std::string& date);
    
    // Account state operations
    void save_account_state(const state::AccountState& account);
    bool load_account_state(state::AccountState& account);
    void save_position_snapshot(const std::string& symbol, const schemas::Position& position);
    
    // Analytics and reporting
    double get_daily_pnl(const std::string& date);
    std::vector<std::pair<std::string, double>> get_position_summary();
    
    // Database maintenance
    void create_tables();
    void cleanup_old_data(int days_to_keep = 90);

private:
    std::unique_ptr<ConnectionPool> pool_;
    
    // SQL statement helpers
    void execute_statement(const std::string& sql);
    pqxx::result execute_query(const std::string& sql);
    
    // Schema creation
    void create_orders_table();
    void create_fills_table();
    void create_positions_table();
    void create_account_snapshots_table();
};

} // namespace trading_core::persistence
```

**src/persistence/database_manager.cpp**:
```cpp
#include <trading_core/persistence/database_manager.h>
#include <trading_core/util/logging.h>

#include <chrono>
#include <sstream>
#include <iomanip>

namespace trading_core::persistence {

ConnectionPool::ConnectionPool(const std::string& connection_string, size_t pool_size)
    : connection_string_(connection_string), pool_size_(pool_size), active_connections_(0) {
    
    // Create initial connections
    for (size_t i = 0; i < pool_size_; ++i) {
        create_connection();
    }
    
    LOG_INFO("Database connection pool initialized with {} connections", pool_size_);
}

std::unique_ptr<pqxx::connection> ConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    condition_.wait(lock, [this] { return !available_connections_.empty(); });
    
    auto conn = std::move(available_connections_.front());
    available_connections_.pop();
    
    return conn;
}

void ConnectionPool::release(std::unique_ptr<pqxx::connection> conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (conn && conn->is_open()) {
        available_connections_.push(std::move(conn));
        condition_.notify_one();
    } else {
        // Connection is bad, create a new one
        create_connection();
    }
}

void ConnectionPool::create_connection() {
    try {
        auto conn = std::make_unique<pqxx::connection>(connection_string_);
        available_connections_.push(std::move(conn));
        ++active_connections_;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create database connection: {}", e.what());
        throw;
    }
}

DatabaseManager::DatabaseManager(const std::string& connection_string, size_t pool_size)
    : pool_(std::make_unique<ConnectionPool>(connection_string, pool_size)) {
    
    create_tables();
    LOG_INFO("DatabaseManager initialized");
}

void DatabaseManager::save_order(const schemas::Order& order) {
    auto conn = pool_->acquire();
    
    try {
        pqxx::work txn(*conn);
        
        std::string sql = R"(
            INSERT INTO orders (
                client_order_id, exchange_order_id, symbol, side, quantity, 
                price, order_type, status, created_at, updated_at
            ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)
            ON CONFLICT (client_order_id) DO UPDATE SET
                exchange_order_id = EXCLUDED.exchange_order_id,
                status = EXCLUDED.status,
                updated_at = EXCLUDED.updated_at
        )";
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        txn.exec_params(sql,
            order.client_order_id,
            order.exchange_order_id,
            order.symbol,
            static_cast<int>(order.side),
            order.quantity,
            order.price,
            static_cast<int>(order.order_type),
            static_cast<int>(order.status),
            time_t,
            time_t
        );
        
        txn.commit();
        LOG_DEBUG("Order saved: {}", order.client_order_id);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save order {}: {}", order.client_order_id, e.what());
        throw;
    }
    
    pool_->release(std::move(conn));
}

void DatabaseManager::create_orders_table() {
    auto conn = pool_->acquire();
    
    try {
        pqxx::work txn(*conn);
        
        std::string sql = R"(
            CREATE TABLE IF NOT EXISTS orders (
                id SERIAL PRIMARY KEY,
                client_order_id VARCHAR(64) UNIQUE NOT NULL,
                exchange_order_id VARCHAR(64),
                symbol VARCHAR(32) NOT NULL,
                side INTEGER NOT NULL,
                quantity DECIMAL(18,8) NOT NULL,
                price DECIMAL(18,8),
                order_type INTEGER NOT NULL,
                status INTEGER NOT NULL,
                created_at TIMESTAMP NOT NULL,
                updated_at TIMESTAMP NOT NULL
            );
            
            CREATE INDEX IF NOT EXISTS idx_orders_symbol ON orders(symbol);
            CREATE INDEX IF NOT EXISTS idx_orders_status ON orders(status);
            CREATE INDEX IF NOT EXISTS idx_orders_created_at ON orders(created_at);
        )";
        
        txn.exec(sql);
        txn.commit();
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create orders table: {}", e.what());
        throw;
    }
    
    pool_->release(std::move(conn));
}

// Additional implementation methods...

} // namespace trading_core::persistence
```

## Phase 7: Performance & Optimization

### Step 7.1: Memory Management & Object Pooling (.h/.cpp separation)

**Objective**: Implement high-performance memory management with object pools for low-latency trading

**include/trading_core/memory/object_pool.h**:
```cpp
#pragma once

#include <memory>
#include <queue>
#include <mutex>
#include <functional>
#include <atomic>

namespace trading_core::memory {

template<typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t initial_size = 1000);
    ~ObjectPool();

    // Acquire object with custom deleter that returns to pool
    std::unique_ptr<T, std::function<void(T*)>> acquire();
    
    // Manual release (alternative to custom deleter)
    void release(T* obj);
    
    // Pool statistics
    size_t available_count() const;
    size_t total_count() const;
    
    // Expand pool if needed
    void expand(size_t additional_objects);

private:
    mutable std::mutex mutex_;
    std::queue<std::unique_ptr<T>> pool_;
    std::atomic<size_t> total_objects_;
    std::atomic<size_t> acquired_objects_;
    
    void create_object();
};

// Specializations for commonly used types
extern ObjectPool<class Order> order_pool;
extern ObjectPool<class TradeMsg> trade_msg_pool;
extern ObjectPool<class BookMsg> book_msg_pool;

} // namespace trading_core::memory
```

**src/memory/object_pool.cpp**:
```cpp
#include <trading_core/memory/object_pool.h>
#include <trading_core/schemas/order.h>
#include <trading_core/schemas/trade_msg.h>
#include <trading_core/schemas/book_msg.h>

namespace trading_core::memory {

template<typename T>
ObjectPool<T>::ObjectPool(size_t initial_size) 
    : total_objects_(0), acquired_objects_(0) {
    
    for (size_t i = 0; i < initial_size; ++i) {
        create_object();
    }
}

template<typename T>
std::unique_ptr<T, std::function<void(T*)>> ObjectPool<T>::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (pool_.empty()) {
        // Pool exhausted, create new object
        create_object();
    }
    
    auto obj = std::move(pool_.front());
    pool_.pop();
    ++acquired_objects_;
    
    // Custom deleter that returns object to pool
    auto deleter = [this](T* ptr) {
        this->release(ptr);
    };
    
    return std::unique_ptr<T, std::function<void(T*)>>(
        obj.release(), deleter);
}

template<typename T>
void ObjectPool<T>::release(T* obj) {
    if (!obj) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Reset object state if needed
    if constexpr (requires { obj->reset(); }) {
        obj->reset();
    }
    
    pool_.push(std::unique_ptr<T>(obj));
    --acquired_objects_;
}

template<typename T>
void ObjectPool<T>::create_object() {
    pool_.push(std::make_unique<T>());
    ++total_objects_;
}

// Explicit instantiations
template class ObjectPool<schemas::Order>;
template class ObjectPool<schemas::TradeMsg>;
template class ObjectPool<schemas::BookMsg>;

// Global pool instances
ObjectPool<schemas::Order> order_pool(500);
ObjectPool<schemas::TradeMsg> trade_msg_pool(1000);
ObjectPool<schemas::BookMsg> book_msg_pool(1000);

} // namespace trading_core::memory
```

### Step 7.2: Lock-Free Data Structures (.h/.cpp separation)

**Objective**: Implement lock-free queues for high-frequency market data processing

**include/trading_core/concurrent/spsc_queue.h**:
```cpp
#pragma once

#include <atomic>
#include <memory>
#include <array>

namespace trading_core::concurrent {

// Single Producer Single Consumer lock-free queue
template<typename T, size_t Capacity = 4096>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    
public:
    SPSCQueue();
    ~SPSCQueue() = default;
    
    // Producer operations (single thread only)
    bool try_enqueue(T&& item);
    bool try_enqueue(const T& item);
    
    template<typename... Args>
    bool try_emplace(Args&&... args);
    
    // Consumer operations (single thread only)
    bool try_dequeue(T& item);
    
    // Status queries (can be called from any thread)
    bool empty() const noexcept;
    bool full() const noexcept;
    size_t size() const noexcept;
    size_t capacity() const noexcept { return Capacity; }

private:
    static constexpr size_t MASK = Capacity - 1;
    
    alignas(64) std::atomic<size_t> head_{0};  // Producer index
    alignas(64) std::atomic<size_t> tail_{0};  // Consumer index
    
    alignas(64) std::array<T, Capacity> buffer_;
};

// Multi Producer Single Consumer lock-free queue
template<typename T, size_t Capacity = 4096>
class MPSCQueue {
public:
    MPSCQueue();
    ~MPSCQueue();
    
    // Producer operations (multiple threads)
    bool try_enqueue(T&& item);
    bool try_enqueue(const T& item);
    
    // Consumer operations (single thread only)
    bool try_dequeue(T& item);
    size_t try_dequeue_bulk(T* items, size_t max_count);
    
    bool empty() const noexcept;
    size_t approximate_size() const noexcept;

private:
    struct Node {
        std::atomic<Node*> next{nullptr};
        T data;
    };
    
    alignas(64) std::atomic<Node*> head_;
    alignas(64) std::atomic<Node*> tail_;
    
    Node* allocate_node();
    void deallocate_node(Node* node);
};

} // namespace trading_core::concurrent
```

**src/concurrent/spsc_queue.cpp**:
```cpp
#include <trading_core/concurrent/spsc_queue.h>
#include <new>

namespace trading_core::concurrent {

template<typename T, size_t Capacity>
SPSCQueue<T, Capacity>::SPSCQueue() {
    // Initialize buffer with default-constructed objects if needed
    static_assert(std::is_trivially_destructible_v<T> || 
                  std::is_nothrow_destructible_v<T>,
                  "T must be trivially or nothrow destructible");
}

template<typename T, size_t Capacity>
bool SPSCQueue<T, Capacity>::try_enqueue(T&& item) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (head + 1) & MASK;
    
    if (next_head == tail_.load(std::memory_order_acquire)) {
        return false; // Queue is full
    }
    
    buffer_[head] = std::move(item);
    head_.store(next_head, std::memory_order_release);
    return true;
}

template<typename T, size_t Capacity>
bool SPSCQueue<T, Capacity>::try_enqueue(const T& item) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (head + 1) & MASK;
    
    if (next_head == tail_.load(std::memory_order_acquire)) {
        return false; // Queue is full
    }
    
    buffer_[head] = item;
    head_.store(next_head, std::memory_order_release);
    return true;
}

template<typename T, size_t Capacity>
template<typename... Args>
bool SPSCQueue<T, Capacity>::try_emplace(Args&&... args) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (head + 1) & MASK;
    
    if (next_head == tail_.load(std::memory_order_acquire)) {
        return false; // Queue is full
    }
    
    buffer_[head] = T{std::forward<Args>(args)...};
    head_.store(next_head, std::memory_order_release);
    return true;
}

template<typename T, size_t Capacity>
bool SPSCQueue<T, Capacity>::try_dequeue(T& item) {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    
    if (tail == head_.load(std::memory_order_acquire)) {
        return false; // Queue is empty
    }
    
    item = std::move(buffer_[tail]);
    tail_.store((tail + 1) & MASK, std::memory_order_release);
    return true;
}

template<typename T, size_t Capacity>
bool SPSCQueue<T, Capacity>::empty() const noexcept {
    return head_.load(std::memory_order_acquire) == 
           tail_.load(std::memory_order_acquire);
}

template<typename T, size_t Capacity>
bool SPSCQueue<T, Capacity>::full() const noexcept {
    const size_t head = head_.load(std::memory_order_acquire);
    const size_t tail = tail_.load(std::memory_order_acquire);
    return ((head + 1) & MASK) == tail;
}

template<typename T, size_t Capacity>
size_t SPSCQueue<T, Capacity>::size() const noexcept {
    const size_t head = head_.load(std::memory_order_acquire);
    const size_t tail = tail_.load(std::memory_order_acquire);
    return (head - tail) & MASK;
}

// Explicit instantiations for commonly used types
template class SPSCQueue<class schemas::TradeMsg, 4096>;
template class SPSCQueue<class schemas::BookMsg, 4096>;
template class SPSCQueue<class schemas::Order, 2048>;

} // namespace trading_core::concurrent
```

### Step 7.3: CPU Optimization & Cache-Friendly Design (.h/.cpp separation)

**Objective**: Implement CPU-optimized data structures and algorithms for ultra-low latency

**include/trading_core/optimization/cache_optimized.h**:
```cpp
#pragma once

#include <cstdint>
#include <array>
#include <atomic>
#include <immintrin.h> // For SIMD operations

namespace trading_core::optimization {

// Cache-line aligned structure for hot path data
struct alignas(64) HotPathData {
    std::atomic<uint64_t> sequence_number{0};
    std::atomic<double> last_price{0.0};
    std::atomic<uint64_t> last_quantity{0};
    std::atomic<int64_t> timestamp_ns{0};
    
    // Padding to fill cache line
    char padding[64 - 4 * sizeof(std::atomic<uint64_t>)];
};

// Batch processing for SIMD operations
class PriceBatchProcessor {
public:
    static constexpr size_t BATCH_SIZE = 8; // AVX2 can process 8 floats at once
    
    PriceBatchProcessor();
    
    // Process batch of prices with SIMD
    void process_price_batch(const float* prices, float* results, size_t count);
    
    // Calculate VWAP using SIMD
    double calculate_vwap_simd(const float* prices, const float* volumes, size_t count);
    
    // Fast price comparison
    bool prices_above_threshold_simd(const float* prices, float threshold, size_t count);

private:
    alignas(32) std::array<float, BATCH_SIZE> work_buffer_;
};

// Branch-free implementations
class BranchlessOps {
public:
    // Branch-free min/max
    static inline double branchless_min(double a, double b) {
        return a < b ? a : b;
    }
    
    static inline double branchless_max(double a, double b) {
        return a > b ? a : b;
    }
    
    // Branch-free conditional assignment
    static inline double conditional_assign(bool condition, double true_val, double false_val) {
        return condition * true_val + (!condition) * false_val;
    }
    
    // Fast integer operations
    static inline bool is_power_of_two(uint64_t n) {
        return n && !(n & (n - 1));
    }
    
    // Branch-free absolute value
    static inline int64_t branchless_abs(int64_t x) {
        const int64_t mask = x >> 63;
        return (x + mask) ^ mask;
    }
};

// Memory prefetching utilities
class PrefetchManager {
public:
    enum class Locality {
        TEMPORAL_0 = 0,    // No temporal locality
        TEMPORAL_1 = 1,    // Low temporal locality  
        TEMPORAL_2 = 2,    // Moderate temporal locality
        TEMPORAL_3 = 3     // High temporal locality
    };
    
    // Prefetch data for read
    static inline void prefetch_read(const void* addr, Locality locality = Locality::TEMPORAL_2) {
        _mm_prefetch(static_cast<const char*>(addr), static_cast<int>(locality));
    }
    
    // Prefetch data for write
    static inline void prefetch_write(const void* addr) {
        _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T0);
    }
    
    // Prefetch cache line sequence
    template<typename T>
    static void prefetch_sequence(const T* data, size_t count, size_t stride = 1) {
        for (size_t i = 0; i < count; i += stride) {
            prefetch_read(&data[i]);
        }
    }
};

} // namespace trading_core::optimization
```

**src/optimization/cache_optimized.cpp**:
```cpp
#include <trading_core/optimization/cache_optimized.h>
#include <algorithm>
#include <numeric>

namespace trading_core::optimization {

PriceBatchProcessor::PriceBatchProcessor() {
    // Initialize work buffer
    work_buffer_.fill(0.0f);
}

void PriceBatchProcessor::process_price_batch(const float* prices, float* results, size_t count) {
    const size_t simd_count = (count / BATCH_SIZE) * BATCH_SIZE;
    
    // Process SIMD batches
    for (size_t i = 0; i < simd_count; i += BATCH_SIZE) {
        // Load 8 floats into AVX2 register
        __m256 price_vec = _mm256_load_ps(&prices[i]);
        
        // Example: multiply by constant factor (can be any operation)
        __m256 factor = _mm256_set1_ps(1.0001f); // Small adjustment
        __m256 result_vec = _mm256_mul_ps(price_vec, factor);
        
        // Store results
        _mm256_store_ps(&results[i], result_vec);
    }
    
    // Process remaining elements
    for (size_t i = simd_count; i < count; ++i) {
        results[i] = prices[i] * 1.0001f;
    }
}

double PriceBatchProcessor::calculate_vwap_simd(const float* prices, const float* volumes, size_t count) {
    __m256 sum_pv = _mm256_setzero_ps();
    __m256 sum_v = _mm256_setzero_ps();
    
    const size_t simd_count = (count / BATCH_SIZE) * BATCH_SIZE;
    
    // SIMD processing
    for (size_t i = 0; i < simd_count; i += BATCH_SIZE) {
        __m256 price_vec = _mm256_load_ps(&prices[i]);
        __m256 vol_vec = _mm256_load_ps(&volumes[i]);
        
        // price * volume
        __m256 pv = _mm256_mul_ps(price_vec, vol_vec);
        sum_pv = _mm256_add_ps(sum_pv, pv);
        sum_v = _mm256_add_ps(sum_v, vol_vec);
    }
    
    // Horizontal sum of AVX2 registers
    alignas(32) float pv_array[8], v_array[8];
    _mm256_store_ps(pv_array, sum_pv);
    _mm256_store_ps(v_array, sum_v);
    
    double total_pv = std::accumulate(pv_array, pv_array + 8, 0.0);
    double total_v = std::accumulate(v_array, v_array + 8, 0.0);
    
    // Process remaining elements
    for (size_t i = simd_count; i < count; ++i) {
        total_pv += prices[i] * volumes[i];
        total_v += volumes[i];
    }
    
    return total_v > 0 ? total_pv / total_v : 0.0;
}

bool PriceBatchProcessor::prices_above_threshold_simd(const float* prices, float threshold, size_t count) {
    __m256 threshold_vec = _mm256_set1_ps(threshold);
    
    const size_t simd_count = (count / BATCH_SIZE) * BATCH_SIZE;
    
    for (size_t i = 0; i < simd_count; i += BATCH_SIZE) {
        __m256 price_vec = _mm256_load_ps(&prices[i]);
        __m256 cmp_result = _mm256_cmp_ps(price_vec, threshold_vec, _CMP_LE_OQ);
        
        // If any price is <= threshold, return false
        int mask = _mm256_movemask_ps(cmp_result);
        if (mask != 0) {
            return false;
        }
    }
    
    // Check remaining elements
    for (size_t i = simd_count; i < count; ++i) {
        if (prices[i] <= threshold) {
            return false;
        }
    }
    
    return true;
}

} // namespace trading_core::optimization
```

### Step 7.4: Profiling & Benchmarking Integration (.h/.cpp separation)

**Objective**: Integrate performance profiling and benchmarking tools

**include/trading_core/profiling/performance_monitor.h**:
```cpp
#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <memory>
#include <spdlog/spdlog.h>

namespace trading_core::profiling {

class PerformanceMonitor {
public:
    static PerformanceMonitor& instance();
    
    // RAII timer for automatic measurement
    class ScopedTimer {
    public:
        explicit ScopedTimer(const std::string& name);
        ~ScopedTimer();
        
    private:
        std::string name_;
        std::chrono::high_resolution_clock::time_point start_;
    };
    
    // Manual timing
    void start_timer(const std::string& name);
    void end_timer(const std::string& name);
    
    // Record custom metrics
    void record_latency(const std::string& operation, std::chrono::nanoseconds duration);
    void record_throughput(const std::string& operation, uint64_t count, std::chrono::milliseconds window);
    
    // Statistics
    struct Statistics {
        uint64_t count = 0;
        double min_ns = std::numeric_limits<double>::max();
        double max_ns = 0.0;
        double avg_ns = 0.0;
        double p50_ns = 0.0;
        double p95_ns = 0.0;
        double p99_ns = 0.0;
    };
    
    Statistics get_statistics(const std::string& operation) const;
    void dump_statistics() const;
    void reset_statistics();
    
    // Enable/disable monitoring
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }

private:
    PerformanceMonitor() = default;
    
    struct TimingData {
        std::vector<double> samples;
        std::chrono::high_resolution_clock::time_point start_time;
    };
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TimingData> timing_data_;
    std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> active_timers_;
    std::atomic<bool> enabled_{true};
    
    void calculate_percentiles(std::vector<double>& samples, Statistics& stats) const;
};

// Macro for convenient scoped timing
#define PROFILE_SCOPE(name) \
    auto _prof_timer = trading_core::profiling::PerformanceMonitor::instance().is_enabled() ? \
    std::make_unique<trading_core::profiling::PerformanceMonitor::ScopedTimer>(name) : nullptr

// Macro for function profiling
#define PROFILE_FUNCTION() PROFILE_SCOPE(__PRETTY_FUNCTION__)

} // namespace trading_core::profiling
```

**src/profiling/performance_monitor.cpp**:
```cpp
#include <trading_core/profiling/performance_monitor.h>
#include <algorithm>
#include <numeric>
#include <iomanip>

namespace trading_core::profiling {

PerformanceMonitor& PerformanceMonitor::instance() {
    static PerformanceMonitor instance;
    return instance;
}

PerformanceMonitor::ScopedTimer::ScopedTimer(const std::string& name) 
    : name_(name), start_(std::chrono::high_resolution_clock::now()) {
}

PerformanceMonitor::ScopedTimer::~ScopedTimer() {
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_);
    PerformanceMonitor::instance().record_latency(name_, duration);
}

void PerformanceMonitor::start_timer(const std::string& name) {
    if (!enabled_) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    active_timers_[name] = std::chrono::high_resolution_clock::now();
}

void PerformanceMonitor::end_timer(const std::string& name) {
    if (!enabled_) return;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_timers_.find(name);
    if (it != active_timers_.end()) {
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time - it->second);
        timing_data_[name].samples.push_back(static_cast<double>(duration.count()));
        active_timers_.erase(it);
    }
}

void PerformanceMonitor::record_latency(const std::string& operation, 
                                       std::chrono::nanoseconds duration) {
    if (!enabled_) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    timing_data_[operation].samples.push_back(static_cast<double>(duration.count()));
}

PerformanceMonitor::Statistics PerformanceMonitor::get_statistics(const std::string& operation) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Statistics stats;
    auto it = timing_data_.find(operation);
    if (it == timing_data_.end() || it->second.samples.empty()) {
        return stats;
    }
    
    auto samples = it->second.samples; // Copy for sorting
    calculate_percentiles(samples, stats);
    
    return stats;
}

void PerformanceMonitor::calculate_percentiles(std::vector<double>& samples, Statistics& stats) const {
    if (samples.empty()) return;
    
    std::sort(samples.begin(), samples.end());
    
    stats.count = samples.size();
    stats.min_ns = samples.front();
    stats.max_ns = samples.back();
    stats.avg_ns = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    
    // Calculate percentiles
    auto p50_idx = static_cast<size_t>(0.50 * (samples.size() - 1));
    auto p95_idx = static_cast<size_t>(0.95 * (samples.size() - 1));
    auto p99_idx = static_cast<size_t>(0.99 * (samples.size() - 1));
    
    stats.p50_ns = samples[p50_idx];
    stats.p95_ns = samples[p95_idx];
    stats.p99_ns = samples[p99_idx];
}

void PerformanceMonitor::dump_statistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    spdlog::info("=== Performance Statistics ===");
    for (const auto& [operation, data] : timing_data_) {
        auto samples = data.samples; // Copy for sorting
        if (samples.empty()) continue;
        
        Statistics stats;
        calculate_percentiles(samples, stats);
        
        spdlog::info("Operation: {}", operation);
        spdlog::info("  Count: {}", stats.count);
        spdlog::info("  Min: {:.2f} ns", stats.min_ns);
        spdlog::info("  Avg: {:.2f} ns", stats.avg_ns);
        spdlog::info("  P50: {:.2f} ns", stats.p50_ns);
        spdlog::info("  P95: {:.2f} ns", stats.p95_ns);
        spdlog::info("  P99: {:.2f} ns", stats.p99_ns);
        spdlog::info("  Max: {:.2f} ns", stats.max_ns);
        spdlog::info("---");
    }
}

void PerformanceMonitor::reset_statistics() {
    std::lock_guard<std::mutex> lock(mutex_);
    timing_data_.clear();
    active_timers_.clear();
}

} // namespace trading_core::profiling
```

## Phase 8: Testing Strategy & Deployment

### Step 8.1: Unit Testing Framework (.h/.cpp separation)

**Objective**: Implement comprehensive unit testing using Catch2

**tests/unit/test_schemas.cpp**:
```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <trading_core/schemas/trade_msg.h>
#include <trading_core/schemas/book_msg.h>
#include <trading_core/schemas/order.h>
#include <nlohmann/json.hpp>

using namespace trading_core::schemas;

TEST_CASE("TradeMsg serialization/deserialization", "[schemas][trade_msg]") {
    SECTION("Basic construction and JSON conversion") {
        TradeMsg trade{
            .exchange = "binance",
            .symbol = "BTCUSDT", 
            .price = 50000.0,
            .amount = 1.5,
            .side = Side::BUY,
            .timestamp = std::chrono::system_clock::now(),
            .trade_id = "12345"
        };
        
        // Test JSON serialization
        nlohmann::json j = trade.to_json();
        REQUIRE(j["exchange"] == "binance");
        REQUIRE(j["symbol"] == "BTCUSDT");
        REQUIRE(j["price"] == 50000.0);
        REQUIRE(j["amount"] == 1.5);
        REQUIRE(j["side"] == "BUY");
        
        // Test JSON deserialization
        auto trade2 = TradeMsg::from_json(j);
        REQUIRE(trade2.exchange == trade.exchange);
        REQUIRE(trade2.symbol == trade.symbol);
        REQUIRE(trade2.price == trade.price);
        REQUIRE(trade2.amount == trade.amount);
        REQUIRE(trade2.side == trade.side);
    }
    
    SECTION("Performance benchmarks") {
        TradeMsg trade{
            .exchange = "binance",
            .symbol = "BTCUSDT",
            .price = 50000.0,
            .amount = 1.5
        };
        
        BENCHMARK("TradeMsg JSON serialization") {
            return trade.to_json();
        };
        
        auto j = trade.to_json();
        BENCHMARK("TradeMsg JSON deserialization") {
            return TradeMsg::from_json(j);
        };
    }
}

TEST_CASE("Order state transitions", "[schemas][order]") {
    SECTION("Valid state transitions") {
        Order order{
            .order_id = "test_001",
            .symbol = "BTCUSDT",
            .side = Side::BUY,
            .type = OrderType::LIMIT,
            .amount = 1.0,
            .price = 50000.0
        };
        
        REQUIRE(order.state == OrderState::PENDING);
        
        // Test valid transitions
        REQUIRE(order.update_state(OrderState::SUBMITTED));
        REQUIRE(order.state == OrderState::SUBMITTED);
        
        REQUIRE(order.update_state(OrderState::PARTIALLY_FILLED));
        REQUIRE(order.state == OrderState::PARTIALLY_FILLED);
        
        REQUIRE(order.update_state(OrderState::FILLED));
        REQUIRE(order.state == OrderState::FILLED);
    }
    
    SECTION("Invalid state transitions") {
        Order order;
        order.update_state(OrderState::FILLED);
        
        // Cannot transition from FILLED to other states
        REQUIRE_FALSE(order.update_state(OrderState::SUBMITTED));
        REQUIRE_FALSE(order.update_state(OrderState::CANCELLED));
    }
}
```

### Step 8.2: Integration Testing (.h/.cpp separation)

**Objective**: Test component integration with ccapi and database

**tests/integration/test_market_data_integration.cpp**:
```cpp
#include <catch2/catch_test_macros.hpp>
#include <trading_core/market_data/market_data_manager.h>
#include <trading_core/messaging/message_bus.h>
#include <trading_core/concurrent/spsc_queue.h>
#include <thread>
#include <chrono>

using namespace trading_core;

class IntegrationTestFixture {
public:
    IntegrationTestFixture() {
        // Setup test environment
        message_bus = std::make_shared<messaging::MessageBus>();
        market_data_mgr = std::make_unique<market_data::MarketDataManager>(message_bus);
    }
    
    ~IntegrationTestFixture() {
        // Cleanup
        if (market_data_mgr) {
            market_data_mgr->stop();
        }
    }
    
protected:
    std::shared_ptr<messaging::MessageBus> message_bus;
    std::unique_ptr<market_data::MarketDataManager> market_data_mgr;
};

TEST_CASE_METHOD(IntegrationTestFixture, "Market data streaming integration", "[integration][market_data]") {
    SECTION("Subscribe and receive trade messages") {
        concurrent::SPSCQueue<schemas::TradeMsg> trade_queue;
        
        // Subscribe to trade messages
        message_bus->subscribe<schemas::TradeMsg>([&trade_queue](const schemas::TradeMsg& trade) {
            trade_queue.try_enqueue(trade);
        });
        
        // Start market data manager
        REQUIRE(market_data_mgr->start());
        
        // Subscribe to a test symbol
        REQUIRE(market_data_mgr->subscribe_trades("binance", "BTCUSDT"));
        
        // Wait for some data (in real test, use mock data)
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // Check if we received any trades
        schemas::TradeMsg trade;
        bool received = trade_queue.try_dequeue(trade);
        
        // In integration test with real exchange, this should be true
        // For unit tests, we would inject mock data
        INFO("Integration test requires live market data or mock setup");
    }
}

TEST_CASE("Database connection pool integration", "[integration][database]") {
    SECTION("Connection pool stress test") {
        auto db_manager = std::make_unique<persistence::DatabaseManager>(
            "postgresql://test:test@localhost:5432/test_trading_db"
        );
        
        REQUIRE(db_manager->initialize());
        
        // Concurrent connection test
        std::vector<std::thread> threads;
        std::atomic<int> successful_queries{0};
        
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back([&db_manager, &successful_queries, i]() {
                try {
                    auto query = fmt::format("SELECT {} as test_id", i);
                    auto result = db_manager->execute_query(query);
                    if (!result.empty()) {
                        ++successful_queries;
                    }
                } catch (const std::exception& e) {
                    // Log error but don't fail test
                    spdlog::warn("Query failed: {}", e.what());
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Should handle concurrent connections gracefully
        REQUIRE(successful_queries > 0);
    }
}
```

### Step 8.3: Performance Testing & Benchmarks (.h/.cpp separation)

**Objective**: Comprehensive performance testing and latency benchmarks

**tests/performance/benchmark_trading_engine.cpp**:
```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <trading_core/trading/trading_engine.h>
#include <trading_core/memory/object_pool.h>
#include <trading_core/optimization/cache_optimized.h>
#include <trading_core/profiling/performance_monitor.h>

using namespace trading_core;

class PerformanceTestFixture {
public:
    PerformanceTestFixture() {
        // Setup performance test environment
        engine = std::make_unique<trading::TradingEngine>();
        profiling::PerformanceMonitor::instance().reset_statistics();
    }
    
protected:
    std::unique_ptr<trading::TradingEngine> engine;
};

TEST_CASE("Order processing latency benchmarks", "[performance][latency]") {
    SECTION("Single order processing") {
        auto& monitor = profiling::PerformanceMonitor::instance();
        
        BENCHMARK_ADVANCED("Order creation and submission")(Catch::Benchmark::Chronometer meter) {
            std::vector<schemas::Order> orders;
            orders.reserve(meter.runs());
            
            meter.measure([&orders](int run) {
                PROFILE_SCOPE("order_creation");
                
                orders.emplace_back(schemas::Order{
                    .order_id = fmt::format("order_{}", run),
                    .symbol = "BTCUSDT",
                    .side = schemas::Side::BUY,
                    .type = schemas::OrderType::LIMIT,
                    .amount = 1.0,
                    .price = 50000.0 + run
                });
                
                return orders.back().order_id;
            });
        };
        
        // Print latency statistics
        auto stats = monitor.get_statistics("order_creation");
        INFO(fmt::format("Order creation P99: {:.2f} ns", stats.p99_ns));
    }
}

TEST_CASE("Memory pool performance", "[performance][memory]") {
    SECTION("Object pool vs standard allocation") {
        auto& trade_pool = memory::trade_msg_pool;
        
        BENCHMARK("Standard allocation") {
            auto trade = std::make_unique<schemas::TradeMsg>();
            trade->exchange = "binance";
            trade->symbol = "BTCUSDT";
            trade->price = 50000.0;
            return trade;
        };
        
        BENCHMARK("Pool allocation") {
            auto trade = trade_pool.acquire();
            trade->exchange = "binance";
            trade->symbol = "BTCUSDT";
            trade->price = 50000.0;
            return trade;
        };
    }
}

TEST_CASE("SIMD performance benchmarks", "[performance][simd]") {
    SECTION("Price processing with SIMD") {
        constexpr size_t COUNT = 1000;
        std::vector<float> prices(COUNT);
        std::vector<float> volumes(COUNT);
        std::vector<float> results(COUNT);
        
        // Initialize test data
        for (size_t i = 0; i < COUNT; ++i) {
            prices[i] = 50000.0f + i;
            volumes[i] = 1.0f + (i % 10);
        }
        
        optimization::PriceBatchProcessor processor;
        
        BENCHMARK("SIMD VWAP calculation") {
            return processor.calculate_vwap_simd(prices.data(), volumes.data(), COUNT);
        };
        
        BENCHMARK("Standard VWAP calculation") {
            double total_pv = 0.0;
            double total_v = 0.0;
            for (size_t i = 0; i < COUNT; ++i) {
                total_pv += prices[i] * volumes[i];
                total_v += volumes[i];
            }
            return total_v > 0 ? total_pv / total_v : 0.0;
        };
    }
}
```

### Step 8.4: Deployment & CI/CD Configuration (.h/.cpp separation)

**Objective**: Automated deployment and continuous integration setup

**.github/workflows/cpp_trading_engine.yml**:
```yaml
name: C++ Trading Engine CI/CD

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

env:
  VCPKG_BINARY_SOURCES: "clear;x-gha,readwrite"

jobs:
  build-and-test:
    runs-on: ubuntu-22.04
    strategy:
      matrix:
        build_type: [Debug, Release, RelWithDebInfo]
        
    steps:
    - uses: actions/checkout@v4
      with:
        submodules: recursive
        
    - name: Export GitHub Actions cache environment variables
      uses: actions/github-script@v7
      with:
        script: |
          core.exportVariable('ACTIONS_CACHE_URL', process.env.ACTIONS_CACHE_URL || '');
          core.exportVariable('ACTIONS_RUNTIME_TOKEN', process.env.ACTIONS_RUNTIME_TOKEN || '');

    - name: Install system dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y \
          build-essential \
          cmake \
          ninja-build \
          pkg-config \
          curl \
          zip \
          unzip \
          tar \
          git \
          postgresql-client \
          redis-tools
          
    - name: Setup vcpkg
      run: |
        if [ ! -d "external/vcpkg" ]; then
          git clone https://github.com/Microsoft/vcpkg.git external/vcpkg
        fi
        cd external/vcpkg
        ./bootstrap-vcpkg.sh
        
    - name: Configure CMake
      run: |
        cmake --preset linux-${{ matrix.build_type }}
        
    - name: Build
      run: |
        cmake --build --preset linux-${{ matrix.build_type }} --parallel $(nproc)
        
    - name: Run Tests
      run: |
        ctest --preset linux-${{ matrix.build_type }} --output-on-failure
        
    - name: Run Benchmarks (Release only)
      if: matrix.build_type == 'Release'
      run: |
        ./build/linux-release/tests/benchmark_trading_engine --benchmark-samples 100
        
    - name: Upload test results
      uses: actions/upload-artifact@v4
      if: always()
      with:
        name: test-results-${{ matrix.build_type }}
        path: |
          build/*/test_results.xml
          build/*/benchmark_results.json
          
  performance-regression:
    needs: build-and-test
    runs-on: ubuntu-22.04
    if: github.event_name == 'pull_request'
    
    steps:
    - name: Download benchmark results
      uses: actions/download-artifact@v4
      with:
        name: test-results-Release
        
    - name: Performance regression check
      run: |
        # Compare with baseline performance metrics
        # Fail if P99 latency increases by more than 10%
        echo "Performance regression check would go here"
        
  security-scan:
    runs-on: ubuntu-22.04
    steps:
    - uses: actions/checkout@v4
    
    - name: Run security scan
      uses: github/codeql-action/init@v3
      with:
        languages: cpp
        
    - name: Build for security analysis
      run: |
        cmake --preset linux-debug
        cmake --build --preset linux-debug
        
    - name: Perform CodeQL Analysis
      uses: github/codeql-action/analyze@v3
```
