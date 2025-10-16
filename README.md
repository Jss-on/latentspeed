# Latentspeed Trading Engine

A high-performance C++ trading engine for algorithmic trading with direct exchange connectivity and comprehensive order execution capabilities. Features custom exchange client architecture, WebSocket support for real-time data, and robust order management.

![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-green.svg)
![ZeroMQ](https://img.shields.io/badge/ZeroMQ-4.3%2B-red.svg)
![Boost](https://img.shields.io/badge/Boost-1.75%2B-orange.svg)
![spdlog](https://img.shields.io/badge/spdlog-1.x-yellow.svg)

## 🚀 Features

### Direct Exchange Connectivity
- **Custom Exchange Client Architecture**: Native REST and WebSocket integration
- **Bybit Integration**: Full support for spot and perpetual trading (testnet/mainnet)
- **WebSocket Real-Time Updates**: Order status updates and fills via WebSocket streams
- **HMAC Authentication**: Secure API key authentication for all requests
- **Connection Management**: Automatic reconnection and heartbeat/ping-pong handling

### Order Management
- **Order Types**: Market and limit orders with time-in-force options
- **Order Actions**: Place, cancel, and modify orders in real-time
- **Order Tracking**: Client order ID mapping and status tracking
- **Fill Reporting**: Real-time execution reports with fee calculations
- **Idempotency Protection**: Duplicate order detection and prevention

### Trading Engine Core
- **Live Trading**: Direct exchange connectivity for spot and perpetual markets
- **Exchange Support**: Currently integrated with Bybit (testnet/mainnet)
- **Risk Management**: Duplicate order detection and validation
- **Multi-threaded Architecture**: Separate threads for order processing, WebSocket streaming, and publishing
- **Error Handling**: Comprehensive error callbacks and exception handling

### Communication Architecture
- **Order Reception**: ZeroMQ PULL socket (`tcp://127.0.0.1:5601`) for ExecutionOrders
- **Report Publishing**: ZeroMQ PUB socket (`tcp://127.0.0.1:5602`) for ExecutionReports and Fills
- **WebSocket Streaming**: Real-time order updates and execution data from exchanges
- **Structured Logging**: spdlog-based logging with file rotation and console output
- **Async Publishing**: Non-blocking message queue for reports and fills

### Order Message Formats

#### CEX Order Example
```json
{
  "version": 1,
  "cl_id": "order_12345",
  "action": "place",
  "venue_type": "cex",
  "venue": "binance",
  "product_type": "spot",
  "details": {
    "symbol": "BTC-USDT",
    "side": "buy",
    "order_type": "limit",
    "time_in_force": "gtc",
    "size": "0.1",
    "price": "50000.0"
  },
  "ts_ns": 1640995200000000000,
  "tags": {
    "strategy": "arbitrage",
    "session": "demo"
  }
}
```

#### ExecutionReport Format
```json
{
  "version": 1,
  "cl_id": "order_12345",
  "status": "accepted",
  "exchange_order_id": "binance_789",
  "reason_code": "ok",
  "reason_text": "Order accepted successfully",
  "ts_ns": 1640995200000002000,
  "tags": {
    "execution_type": "simulated",
    "strategy": "arbitrage"
  }
}
```

#### Fill Report Format
```json
{
  "version": 1,
  "cl_id": "order_12345",
  "exchange_order_id": "binance_789",
  "exec_id": "exec_1640995200_123456",
  "symbol_or_pair": "BTC-USDT",
  "price": 50005.0,
  "size": 0.1,
  "fee_currency": "USDT",
  "fee_amount": 5.0005,
  "liquidity": "taker",
  "ts_ns": 1640995200000003000,
  "tags": {
    "execution_type": "simulated"
  }
}
```

## 🏗️ Architecture

```
┌─────────────────┐   ExecutionOrder   ┌──────────────────────┐   REST API       ┌─────────────┐
│   Trading       │   PUSH->PULL       │  Trading Engine      │◄────────────────►│   Bybit     │
│   Strategies    │   tcp://5601       │  Service             │                  │   Exchange  │
│                 │                    │                      │   WebSocket      │             │
└─────────────────┘                    │  ┌─────────────────┐ │◄────────────────►│  (Testnet/  │
                                       │  │ Exchange Client │ │                  │   Mainnet)  │
┌─────────────────┐   Reports/Fills    │  │ Order Manager   │ │                  └─────────────┘
│   Strategy      │◄──PUB->SUB────────┤  │ WebSocket Handler│ │                  
│   Monitoring    │   tcp://5602       │  │ HMAC Auth       │ │   Callbacks     ┌─────────────┐
└─────────────────┘                    │  └─────────────────┘ │◄────────────────►│   Order     │
                                       │           ▲          │                  │   Updates   │
                                       │           │          │                  │   & Fills   │
                                       │  ┌─────────────────┐ │                  └─────────────┘
                                       │  │ Message Queue   │ │                  
                                       │  │ Async Publisher │ │   Future        ┌─────────────┐
                                       │  │ Error Handler   │ │◄────────────────►│  Additional │
                                       │  └─────────────────┘ │                  │  Exchanges  │
                                       │                      │                  └─────────────┘
                                       └──────────────────────┘

### Key Components

- **Exchange Client Interface**: Abstract base class for exchange implementations
- **Bybit Client**: Full-featured implementation with REST and WebSocket support
- **Order Processing**: Live order execution for spot and perpetual markets
- **WebSocket Handler**: Real-time streaming with automatic reconnection
- **HMAC Authentication**: Secure request signing for all API calls
- **Message Queue**: Async publishing system for reports and fills
- **Error Management**: Comprehensive error handling and callback system

## 🛠️ Build Instructions

### Prerequisites

#### System Dependencies (Ubuntu/WSL)
```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build build-essential pkg-config git python3-dev python3-pip
```

#### Clone Repository
```bash
git clone https://github.com/Jss-on/latentspeed.git
cd latentspeed
```

#### Add vcpkg Submodule
```bash
git submodule add https://github.com/microsoft/vcpkg.git external/vcpkg
cd external/vcpkg
./bootstrap-vcpkg.sh
cd ../..
```

### Build Process

#### Build the Project
```bash
# Debug build
./run.sh --debug

# Release build  
./run.sh --release
```

## 🚀 Running the Trading Engine

### Start the Service
```bash
# From build directory
./trading_engine_service

# Or with full path
./build/trading_engine_service
```

### Expected Output
```
=== Latentspeed Trading Engine Service ===
Starting up...
[TradingEngine] Live trading engine initialized
[TradingEngine] Mode: Live exchange connectivity
[TradingEngine] Creating ZeroMQ context...
[TradingEngine] Order receiver socket bound to tcp://127.0.0.1:5601
[TradingEngine] Report publisher socket bound to tcp://127.0.0.1:5602
[TradingEngine] Initializing Bybit client...
[TradingEngine] Bybit client initialized for demo environment
[TradingEngine] Connected to Bybit demo environment
[TradingEngine] Subscribed to order updates
[TradingEngine] Live trading initialization complete
[TradingEngine] Order receiver thread started
[TradingEngine] Publisher thread started
[Main] Trading engine started successfully
[Main] Listening for orders on tcp://127.0.0.1:5601
[Main] Publishing reports on tcp://127.0.0.1:5602
[Main] Press Ctrl+C to stop
```

### Service Configuration

**Default Endpoints:**
- **Order Reception**: `tcp://127.0.0.1:5601` (PULL socket for ExecutionOrders)
- **Report Publishing**: `tcp://127.0.0.1:5602` (PUB socket for ExecutionReports/Fills)

**Exchange Configuration:**
- **Bybit Testnet**: Default configuration with demo credentials
- **Bybit Mainnet**: Configurable via API key/secret initialization
- **WebSocket Streams**: Automatic subscription to order and execution updates

**Connection Features:**
- **Auto-Reconnection**: Automatic WebSocket reconnection on disconnect
- **Heartbeat**: Ping/pong mechanism to maintain connection (20s interval)
- **Request Signing**: HMAC-SHA256 authentication for all requests

## 🧪 Testing the Service

### Python Client Examples

#### Send ExecutionOrder (PUSH socket)
```python
import zmq
import json
import time

# Connect to trading engine order receiver
context = zmq.Context()
socket = context.socket(zmq.PUSH)
socket.connect("tcp://localhost:5601")

# CEX Limit Order
order = {
    "version": 1,
    "cl_id": f"test_order_{int(time.time())}",
    "action": "place",
    "venue_type": "cex",
    "venue": "bybit",
    "product_type": "spot",
    "details": {
        "symbol": "BTC-USDT",
        "side": "buy",
        "order_type": "limit",
        "time_in_force": "GTC",
        "size": "0.001",
        "price": "50000.0"
    },
    "ts_ns": int(time.time() * 1e9),
    "tags": {"test": "demo"}
}

socket.send_string(json.dumps(order))
print(f"Sent order: {order['cl_id']}")
```

#### Monitor ExecutionReports and Fills (SUB socket)
```python
import zmq
import json

# Subscribe to execution reports and fills
context = zmq.Context()
socket = context.socket(zmq.SUB)
socket.connect("tcp://localhost:5602")
socket.setsockopt(zmq.SUBSCRIBE, b"")  # Subscribe to all messages

print("Monitoring execution reports and fills...")
while True:
    try:
        message = socket.recv_string(zmq.NOBLOCK)
        data = json.loads(message)
        
        if "status" in data:  # ExecutionReport
            print(f"ExecutionReport: {data['cl_id']} -> {data['status']}")
        elif "exec_id" in data:  # Fill
            print(f"Fill: {data['cl_id']} -> {data['size']}@{data['price']}")
            
    except zmq.Again:
        time.sleep(0.1)
    except KeyboardInterrupt:
        break
```

### Live Trading Mode

The engine operates in live trading mode with direct exchange connectivity:

- **Exchange Support**: Bybit, Binance Futures (UM), Hyperliquid (DEX)
- **Order Types**: Market and limit orders with various time-in-force options
- **Real-Time Updates**: WebSocket streaming for order status and fills
- **Error Handling**: Comprehensive error reporting and recovery mechanisms

## ⚙️ Configuration

### Service Configuration

The trading engine can be configured via constructor parameters:

```cpp
// Default configuration in TradingEngineService constructor
order_endpoint_("tcp://127.0.0.1:5601")        // Order reception
report_endpoint_("tcp://127.0.0.1:5602")       // Report publishing

// Credentials are resolved centrally from CLI and environment
// CEX (Bybit/Binance): api_key = API key, api_secret = API secret
// DEX (Hyperliquid):   api_key = wallet/user address (0x..), api_secret = private key (hex)
use_testnet = true;                            // Also overridable via env LATENTSPEED_<EXCHANGE>_USE_TESTNET
```

### Exchange Configuration

#### Currently Supported:
- **Bybit**: Full support for spot and perpetual trading
  - Testnet: `testnet.bybit.com`
  - Mainnet: `api.bybit.com`
- **Binance Futures (UM/USDT-M)**: Trading via REST + real-time user-data WS updates
  - Testnet REST: `https://testnet.binancefuture.com` (prefix `/fapi/v1`)
  - Mainnet REST: `https://fapi.binance.com` (prefix `/fapi/v1`)
  - User Data WS (updates/fills):
    - Testnet: `wss://stream.binancefuture.com/ws/<listenKey>`
    - Mainnet: `wss://fstream.binance.com/ws/<listenKey>`
- **Hyperliquid (DEX)**: REST + WS post trading, private WS streams (orderUpdates/userEvents/userFills)
  - Mainnet REST: `https://api.hyperliquid.xyz` / WS: `wss://api.hyperliquid.xyz/ws`
  - Testnet REST: `https://api.hyperliquid-testnet.xyz` / WS: `wss://api.hyperliquid-testnet.xyz/ws`

#### Adding New Exchanges:
1. Implement the `ExchangeClient` interface
2. Add REST API and WebSocket handlers
3. Register in `TradingEngineService::initialize()` (now exchange-agnostic, add to `exchange_clients_` map)

### Credentials & Authentication

Credentials are resolved by a central resolver that merges CLI and environment variables.

#### Bybit API Setup (CEX):
1. Create API key on Bybit (testnet or mainnet)
2. Configure with appropriate permissions (spot/perpetual trading)
3. Provide credentials via CLI `--api-key/--api-secret` or env:
   - `LATENTSPEED_BYBIT_API_KEY`, `LATENTSPEED_BYBIT_API_SECRET`
   - `LATENTSPEED_BYBIT_USE_TESTNET=1|0`

#### Binance Futures API Setup (CEX):
1. Create UM Futures API key (testnet or mainnet) with TRADE and USER_DATA permissions
2. Configure env vars:
   - `LATENTSPEED_BINANCE_API_KEY`, `LATENTSPEED_BINANCE_API_SECRET`
   - `LATENTSPEED_BINANCE_USE_TESTNET=1|0`
3. Optional: `LATENTSPEED_BINANCE_USE_WS_TRADE=1` (WS-API trading stub; REST trading is default)

#### Hyperliquid Credentials (DEX):
Hyperliquid does not issue API keys. Use:
- `LATENTSPEED_HYPERLIQUID_USER_ADDRESS` = 0x… lowercased user/subaccount address
- `LATENTSPEED_HYPERLIQUID_PRIVATE_KEY` = 0x… agent wallet private key (hex)
- `LATENTSPEED_HYPERLIQUID_USE_TESTNET=1|0`

Alternatively pass via CLI as `--api-key` (address) and `--api-secret` (private key). See docs/HYPERLIQUID_ADAPTER_USAGE.md.

#### Security Notes:
- Never log secrets; the signer bridge keeps private keys in its own process memory.
- Use environment variables or a secure vault for production deployments.
- Prefer separate agent wallets per process/subaccount for Hyperliquid.

## 📦 Dependencies

Managed via vcpkg (`vcpkg.json`):
- **openssl**: Secure communications and HMAC signing
- **boost-asio**: Asynchronous I/O operations
- **boost-beast**: HTTP/WebSocket client implementation
- **rapidjson**: JSON parsing for API responses
- **zeromq**: Inter-process communication
- **spdlog**: Structured logging
- **cppzmq**: C++ bindings for ZeroMQ

## 🔧 Development

### Project Structure
```
latentspeed/
├── include/
│   ├── trading_engine_service.h    # Service interface
│   └── exchange/
│       ├── exchange_client.h       # Abstract exchange interface
│       ├── bybit_client.h          # Bybit implementation
│       └── binance_client.h        # Binance implementation
│   ├── adapters/
│   │   ├── bybit_adapter.h/.cpp
│   │   ├── binance_adapter.h/.cpp
│   │   └── hyperliquid_adapter.h/.cpp
│   ├── core/auth/
│   │   ├── auth_provider.h         # Bybit HMAC provider (pilot)
│   │   └── credentials_resolver.h  # Central CEX/DEX credential resolver
├── src/
│   ├── trading_engine_service.cpp  # Service implementation  
│   ├── main.cpp                    # Entry point
│   └── exchange/
│       ├── exchange_client.cpp     # Base implementation
│       ├── bybit_client.cpp        # Bybit client
│       └── binance_client.cpp      # Binance client
├── external/vcpkg/                 # Package manager (submodule)
├── CMakeLists.txt                  # Build configuration
├── CMakePresets.json               # Build presets
└── vcpkg.json                      # Dependencies manifest
```

### Adding New Exchanges
1. Create new class inheriting from `ExchangeClient`
2. Implement all virtual methods (REST API, WebSocket, auth)
3. Add to `exchange_clients_` map in `TradingEngineService`
4. Test with demo/testnet credentials first

### Extending Functionality
- Add new order types in `OrderRequest` structure
- Implement additional exchange clients
- Extend WebSocket message handlers for new data streams
- Add risk management features in order processing

## 📝 License

[Add your license information here]

## 🤝 Contributing

[Add contribution guidelines here]
