# Latentspeed Trading Engine Service — Comprehensive System Report

This report maps the trading engine’s components, connections, data flows, dependencies, configuration, and runtime behavior based on the current repository.

## Executive Summary

- The service is an HFT-optimized C++20+ trading engine with ultra-low latency paths for order ingestion, processing, and publishing.
- Orders come via ZMQ PULL at tcp://127.0.0.1:5601, are parsed to HFT DTOs, routed to venue adapters, and execution reports/fills are published via ZMQ PUB at tcp://127.0.0.1:5602 with lock-free queues and pre-allocated pools.
- Exchange access uses a Phase-1 adapter router ([VenueRouter](cci:2://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/engine/venue_router.h:16:0-33:1)) with an `IExchangeAdapter` interface. Currently wired for Hyperliquid via a bridge adapter that wraps a Hummingbot-style connector.
- Optional market data streaming is provided via a native MarketStream subsystem (separate executable), also using ZMQ.
- Build is via CMake + vcpkg; run via the repository’s build script per project rule.

# Component Map

- **Trading Engine (Core)**
  - Files: src/trading_engine_service.cpp, include/trading_engine_service.h, src/main_trading_engine.cpp
  - Responsibilities: process orders, venue routing, pub/sub, stats, optional market data lifecycle

- **HFT Data Structures**
  - Files: include/hft_data_structures.h
  - Contents: FixedString, MemoryPool<T,N>, LockFreeSPSCQueue<T,N>, FlatMap<Key,Val,N>, HFTExecutionOrder/Report/Fill, PublishMessage, HFTStats

- **Engine Utilities and DTOs**
  - Files: include/engine/exec_dto.h, include/engine/normalized_order.h, include/engine/order_serializer.h, src/engine/order_serializer.cpp, include/action_dispatch.h, include/reason_code_mapper.h, include/utils/hft_utils.h, include/utils/string_utils.h
  - Responsibilities: parse typed order DTOs, serialize reports/fills JSON, fast hashing/action decode, reason-code normalization, TSC timing, string normalization

- **Adapters and Venue Router**
  - Files: include/adapters/exchange_adapter.h, include/engine/venue_router.h
  - Responsibilities: `IExchangeAdapter` abstraction; [VenueRouter](cci:2://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/engine/venue_router.h:16:0-33:1) to register/get adapters by exchange key

- **Hyperliquid Bridge Adapter**
  - Files: include/adapters/hyperliquid/hyperliquid_connector_adapter.h
  - Responsibilities: bridge `IExchangeAdapter` to Hummingbot-style [HyperliquidPerpetualConnector](cci:2://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/connector/exchange/hyperliquid/hyperliquid_perpetual_connector.h:39:0-190:1) (connector framework)

- **Connector Framework (Hummingbot pattern)**
  - Files: include/connector/connector_base.h, include/connector/client_order_tracker.h, include/connector/in_flight_order.h, include/connector/exchange/hyperliquid/* (perpetual connector, auth)
  - Responsibilities: async order lifecycle, order tracking, user streams parsing, (placeholder) Hyperliquid auth/signing

- **Exchange Interface (Engine-facing)**
  - Files: include/exchange/exchange_client.h
  - Responsibilities: OrderRequest/OrderResponse/FillData/OrderUpdate/OpenOrderBrief structures; abstract exchange client surface reused by `IExchangeAdapter`

- **MarketStream (Market Data)**
  - Files: include/marketstream/market_data_provider.h
  - Responsibilities: WebSocket ingestion (Boost.Beast), preprocessing, ZMQ publishing (5556 trades, 5557 orderbooks)

- **Core Mappers and Auth**
  - Files: include/core/symbol/symbol_mapper.h, include/core/reasons/reason_mapper.h, include/core/auth/credentials_resolver.h
  - Responsibilities: symbol normalization, canonical reason mapping, credentials/env resolution

# Process Lifecycle

- **Main Entrypoint**
  - File: src/main_trading_engine.cpp
  - Steps:
    - Parse CLI via `cli::parse_command_line_args`
    - Validate via `cli::validate_config`
    - Initialize logging via `cli::initialize_logging`
    - Construct [TradingEngineService](cci:2://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/trading_engine_service.h:180:0-422:1), call [initialize()](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:194:0-370:1), then [start()](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:372:0-417:1)
    - Run until [stop()](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:419:0-445:1) on SIGINT/SIGTERM

- **Endpoints**
  - Orders in: PULL tcp://127.0.0.1:5601
  - Reports/Fills out: PUB tcp://127.0.0.1:5602
  - MarketStream (separate exec): trades tcp://127.0.0.1:5556, orderbooks tcp://127.0.0.1:5557

# Threads and Concurrency

- **Order Receiver Thread**
  - Method: [hft_order_receiver_thread()](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:447:0-512:1)
  - Non-blocking `recv` on ZMQ PULL; zero-copy parse to [HFTExecutionOrder](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/hft_data_structures.h:365:4-384:5) via pool; routes to process

- **Publisher Thread**
  - Method: [hft_publisher_thread()](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:514:0-547:1)
  - Pops [PublishMessage](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/hft_data_structures.h:461:4-462:65) from SPSC queue and sends PUB frames (topic + payload)

- **Stats Thread**
  - Method: [stats_monitoring_thread()](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:549:0-588:1)
  - Periodic logs every 10s: orders/sec, latency stats, pool availability, queue usage

- **CPU Affinity (Linux)**
  - Order thread pinned to core 2; publisher pinned to core 3
  - Real-time scheduling (SCHED_FIFO priority 80)
  - Memory lock with `mlockall` to prevent paging
  - TSC timing calibration with fallback to chrono

# Data Flow

- **Orders In (ZMQ PULL)**
  - Raw JSON ExecutionOrder messages received at 5601

- **Parsing**
  - [parse_execution_order_hft(std::string_view)](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:594:0-664:1)
  - Parses into `ExecParsed` (include/engine/exec_dto.h), fills [HFTExecutionOrder](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/hft_data_structures.h:365:4-384:5) from pool

- **Processing**
  - [process_execution_order_hft(const HFTExecutionOrder&)](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:666:0-762:1)
  - Action decoding via FNV-1a ([action_dispatch.h](cci:7://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/action_dispatch.h:0:0-0:0)) → dispatch Place/Cancel/Replace

- **Venue Routing and Adapter**
  - [VenueRouter](cci:2://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/engine/venue_router.h:16:0-33:1) resolves adapter by normalized venue key
  - `IExchangeAdapter` implementations handle:
    - place_order
    - cancel_order
    - modify_order
    - query_order
    - list_open_orders

- **Callbacks**
  - Adapter pushes events to engine:
    - [on_order_update_hft(const OrderUpdate&)](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:1099:0-1240:1)
    - [on_fill_hft(const FillData&)](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:1243:0-1314:1)
  - Normalizes status and reason; ensures `venue` tag in reports

- **Publishing**
  - [publish_execution_report_hft](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:1329:0-1349:1) and [publish_fill_hft](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:1351:0-1369:1) serialize JSON via RapidJSON and push to SPSC queue
  - PUB frames:
    - Topic: `exec.report` or `exec.fill`
    - Payload: compact JSON

# HFT Optimizations

- **Memory and Structures**
  - FixedString with stack/inlined storage
  - MemoryPool for orders/reports/fills (pre-warmed)
  - FlatMap for cache-friendly lookups (pending/processed)
  - Lock-free SPSC queues for pub path

- **CPU/OS**
  - TSC-based timestamps (rdtsc/rdtscp), prefetch intrinsics
  - Thread affinity, SCHED_FIFO
  - `mlockall` to lock memory pages (Linux)

- **Stats**
  - Atomic counters, min/max/avg latency tracking

# Exchange Integration

- **Adapter Interface (IExchangeAdapter)**
  - Methods:
    - initialize(api_key, api_secret, testnet)
    - connect/disconnect/is_connected
    - place_order, cancel_order, modify_order, query_order
    - set_order_update_callback, set_fill_callback, set_error_callback
    - get_exchange_name
    - list_open_orders(category, symbol, settle, base_coin)

- **Venue Router**
  - Register adapters, then lookup by venue key

- **Hyperliquid**
  - Engine wiring in [TradingEngineService::initialize()](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:194:0-370:1):
    - Credentials via [auth::resolve_credentials](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/core/auth/credentials_resolver.h:20:0-24:57)
    - Adapter: [HyperliquidConnectorAdapter](cci:2://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/adapters/hyperliquid/hyperliquid_connector_adapter.h:34:0-198:1) (bridge to Hummingbot connector)
    - Callbacks wired to HFT handlers
    - Attempt initial connect; proceed even if not connected
  - Post-connect rehydration:
    - [list_open_orders](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/exchange/exchange_client.h:210:4-225:1) batches for categories linear/inverse/spot to seed `pending_orders_`

- **Order Operations (HFT)**
  - Place:
    - Field validations, category inference, symbol normalization
    - Stop/stop-limit handling:
      - stop → market; stop_limit → limit
      - `triggerPrice`, `triggerDirection`, `orderFilter=StopOrder`
    - Reduce-only enforcement: only for non-spot
    - On success, pending cache insert and acceptance report
  - Cancel:
    - Resolve `cancel_cl_id_to_cancel`, use symbol/exchId from request or pending cache
    - Idempotent handling if venue says order not found
    - Publish synthetic ‘canceled’ for original order
  - Replace:
    - Resolve `replace_cl_id_to_replace`, infer new price/size from tags or fields
    - Modify via adapter; publish acceptance/rejection

- **Update and Fill Callbacks**
  - [on_order_update_hft](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:1099:0-1240:1):
    - Lazy rehydrate unknown live orders via `query_order`
    - Normalize status and map reasons (default or mapper)
    - Publish report, clean up on terminal status
  - [on_fill_hft](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:1243:0-1314:1):
    - Fill fields parsed from strings; copy tags; normalize symbol to hyphen via mapper
    - Publish fill; tag `execution_type` as live/external

# Market Data (MarketStream)

- **Provider**
  - File: include/marketstream/market_data_provider.h
  - WebSocket ingestion (Boost.Beast)
  - Processes trades/books and publishes:
    - Trades → 5556
    - Orderbooks → 5557
  - Thread model: websocket, processing, publishing threads
  - Memory pools + lock-free queues for processing
  - Optional integration in engine (config.enable_market_data)

# Configuration and Credentials

- **TradingEngineConfig**
  - Fields: exchange, api_key, api_secret, live_trade, cpu_mode
  - Market data toggles: enable_market_data, symbols, enable_trades, enable_orderbook

- **CLI**
  - Parsed flags (per main’s header doc): --exchange, --api-key, --api-secret, --live-trade

- **Credentials Resolver**
  - File: include/core/auth/credentials_resolver.h
  - Resolves from CLI + env
  - Hyperliquid required env (engine logs this when missing):
    - LATENTSPEED_HYPERLIQUID_USER_ADDRESS
    - LATENTSPEED_HYPERLIQUID_PRIVATE_KEY

- **Configs (MarketStream)**
  - Files: configs/config.yml, configs/config_hyperliquid.yml, configs/marketstream_hyperliquid.yml
  - Define feeds per exchange and ZMQ publisher configuration

# Build, Dependencies, and Binaries

- **Build (CMake + vcpkg)**
  - Toolchain: external/vcpkg/scripts/buildsystems/vcpkg.cmake
  - Presets: CMakePresets.json (linux-debug, linux-release)
  - Binaries:
    - trading_engine_service
    - marketstream
  - Link: connector_framework, OpenSSL, ZLIB, Threads, ZeroMQ, CURL, Boost::system, RapidJSON, yaml-cpp, spdlog
  - Linux defines: `_GNU_SOURCE`, `HFT_LINUX_FEATURES`

- **Dependencies (vcpkg.json)**
  - openssl, boost-asio, boost-beast, boost-system, rapidjson, zeromq, cppzmq, spdlog, yaml-cpp, args, curl, gtest, nlohmann-json

- **Project Rule (build)**
  - Use the provided script: 
    - ./run.sh --release

# Messaging Schemas

- **Input Order (ExecutionOrder JSON)**
  - Parsed to `ExecParsed`, then mapped to [HFTExecutionOrder](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/hft_data_structures.h:365:4-384:5)
  - Shape:
```json
{
  "version": 1,
  "cl_id": "ORDER-123",
  "action": "place",
  "venue_type": "cex",
  "venue": "hyperliquid",
  "product_type": "perpetual",
  "ts_ns": 0,
  "details": {
    "symbol": "BTC-USDT-PERP",
    "side": "buy",
    "order_type": "limit",
    "time_in_force": "GTC",
    "price": 50000.0,
    "size": 0.01,
    "stop_price": null,
    "reduce_only": false,
    "params": {},
    "cancel": {},
    "replace": {}
  },
  "tags": {
    "strategy": "alpha"
  }
}
```

- **Execution Report (PUB, topic: exec.report)**
```json
{
  "version": 1,
  "cl_id": "ORDER-123",
  "status": "accepted",
  "exchange_order_id": "abc123",
  "reason_code": "ok",
  "reason_text": "Order placed",
  "ts_ns": 1731300000000000000,
  "tags": { "venue": "hyperliquid", "strategy": "alpha" }
}
```

- **Fill (PUB, topic: exec.fill)**
```json
{
  "version": 1,
  "cl_id": "ORDER-123",
  "exchange_order_id": "abc123",
  "exec_id": "fill-1",
  "symbol_or_pair": "BTC-USDT-PERP",
  "price": 50000.0,
  "size": 0.005,
  "fee_currency": "USDT",
  "fee_amount": 0.02,
  "liquidity": "maker",
  "ts_ns": 1731300000000000000,
  "tags": { "venue": "hyperliquid", "strategy": "alpha", "execution_type": "live" }
}
```

# Normalization and Reason Mapping

- **Action Decode**
  - FNV-1a table for “place”, “cancel”, “replace” with compile-time asserts

- **Symbol Mapping**
  - [ISymbolMapper](cci:2://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/core/symbol/symbol_mapper.h:12:0-19:1) → [DefaultSymbolMapper](cci:2://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/core/symbol/symbol_mapper.h:21:0-25:1) transforms to compact or hyphen forms, PERP suffix handling

- **Status Normalization and Reasons**
  - [utils::normalize_report_status](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/utils/string_utils.h:58:0-62:80) → accepted/canceled/rejected/replaced
  - [DefaultReasonMapper](cci:2://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/core/reasons/reason_mapper.h:27:0-32:1) or legacy [reason_code_mapper.h](cci:7://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/reason_code_mapper.h:0:0-0:0) maps raw reasons to canonical codes (e.g., invalid_params, risk_blocked, insufficient_balance, network_error, venue_reject)

# Resilience and Idempotency

- **Duplicate Handling**
  - Dedupe map (`processed_orders_`): 
    - PLACE: if already processed and still pending → ignore; if not pending → allow re-process
    - CANCEL/REPLACE: idempotent re-processing allowed

- **Cancel Idempotency**
  - Treat “not found/unknown order” responses as idempotent success; publish synthetic canceled for original

- **Rehydration**
  - After connect, seeds pending orders by categories and settles via [list_open_orders](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/include/exchange/exchange_client.h:210:4-225:1)

- **Pool/Queue Backpressure**
  - Pool exhaustion increments counters and logs errors
  - Publish queue full increments `queue_full_count` and drops payload with warning

# Security Notes

- **HyperliquidAuth**
  - Placeholder for EIP-712 signing (requires keccak, msgpack, secp256k1). The engine’s build includes auxiliary adapter files and a Python signer bridge (src/adapters/python_hl_signer.cpp, tools/hl_signer_bridge.py) to support signing flows pragmatically.

# Directory Highlights

- **Engine core**: src/trading_engine_service.cpp, include/trading_engine_service.h
- **Order DTOs/Serializer**: include/engine/*.h, src/engine/*.cpp
- **HFT Structures**: include/hft_data_structures.h
- **Utils**: include/utils/*.h, src/utils/*.cpp
- **Adapters**: include/adapters/* (bridge layer)
- **Connector Framework**: include/connector/*, src/connector/*
- **Exchange Abstraction**: include/exchange/exchange_client.h, src/exchange/exchange_client.cpp
- **MarketStream**: include/marketstream/*, src/marketstream/*
- **Configs**: configs/*.yml
- **Build**: CMakeLists.txt, CMakePresets.json, vcpkg.json

# Known Limitations and Next Actions

- **[Current support]** Only Hyperliquid is wired in engine initialize; headers exist for Bybit/Binance adapters but not registered yet.
- **[Stop/Reduce-only]** Reduce-only is rejected for spot; stop orders mapped as venue-acceptable forms with `orderFilter=StopOrder`.
- **[Auth]** Hyperliquid signing is placeholder; ensure production-grade signing (keccak, secp256k1, msgpack) or keep Python signer bridge in place.
- **[Observability]** Stats are logged via spdlog; consider Prometheus/gRPC metrics if needed.
- **[Extensibility]** Add more `IExchangeAdapter` backends and register in [initialize()](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:194:0-370:1); expand reason/symbol mappers as needed.

# Build and Run

- **Build (per project rule)**
  - Run: 
    - ./run.sh --release
- **Run engine**
  - Binary: trading_engine_service
  - Typical flags: --exchange hyperliquid --api-key <key_or_address> --api-secret <secret_or_pk> --live-trade
- **Run market data (optional)**
  - Binary: marketstream
  - Config: configs/config.yml (or hyperliquid-specific configs provided)

# Appendices

- **IExchangeAdapter Summary**
  - place_order/cancel_order/modify_order/query_order
  - set_order_update_callback/set_fill_callback/set_error_callback
  - list_open_orders for rehydration
  - get_exchange_name for router key

- **HFT DTOs**
  - HFTExecutionOrder: fixed fields, tags/params FlatMap
  - HFTExecutionReport: status, reason_code/text, tags
  - HFTFill: price, size, fees, tags

- **Topics**
  - exec.report
  - exec.fill

- **ZMQ**
  - PUB sends topic and payload frames separately; subscribers can subscribe to topic prefix or empty string for all

# Recommended Actions

- **[Register more adapters]** Wire Bybit/Binance adapters into [TradingEngineService::initialize()](cci:1://file://wsl.localhost/Ubuntu-22.04/home/tensor/latentspeed/src/trading_engine_service.cpp:194:0-370:1) with appropriate credential resolution.
- **[Finalize signing]** Replace HyperliquidAuth placeholders with production crypto or ensure Python signer bridge is robust.
- **[Config hardening]** Provide a sample CLI + env matrix in README for all venues, including testnet flags.
- **[Observability]** Add metrics export (e.g., Prometheus) and inline tracing in hot paths behind compile flags.

# Task Status

- **Completed** deep research on components, connections, dependencies, flows, and produced a comprehensive system report.
- All planning todos were executed and marked completed.