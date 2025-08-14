# Latentspeed Trading Engine

A high-performance trading engine for algorithmic trading across centralized exchanges (CEX), decentralized exchanges (DEX), and on-chain operations. The engine provides unified order execution, real-time market data processing, and comprehensive backtest simulation capabilities.

![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-green.svg)
![ZeroMQ](https://img.shields.io/badge/ZeroMQ-4.3%2B-red.svg)
![Doxygen](https://img.shields.io/badge/docs-Doxygen-blue.svg)

## 🚀 Features

### Multi-Venue Support
- **Centralized Exchanges (CEX)**: Binance, Bybit, OKX, and others via ccapi
- **Decentralized Exchanges (DEX)**: Uniswap V2/V3, SushiSwap, PancakeSwap via Hummingbot Gateway
- **On-Chain Operations**: Direct token transfers on Ethereum, BSC, and other networks
- **Unified Interface**: Single ExecutionOrder format for all venue types

### Advanced Trading Features
- **Multiple Order Types**: Market, limit, stop, stop-limit orders
- **AMM and CLMM Swaps**: Support for both traditional AMM and concentrated liquidity protocols
- **Cross-Chain Operations**: Token transfers and arbitrage across different blockchains
- **Backtest Simulation**: Realistic order fill simulation using live market data
- **Risk Management**: Order validation, duplicate detection, and position tracking

### Communication Architecture
- **Order Reception**: ZeroMQ PULL socket (`tcp://127.0.0.1:5601`) for ExecutionOrders
- **Report Publishing**: ZeroMQ PUB socket (`tcp://127.0.0.1:5602`) for ExecutionReports and Fills
- **Market Data Feeds**: Preprocessed trade and orderbook data via ZeroMQ SUB sockets
- **Gateway Integration**: REST API communication with Hummingbot Gateway for DEX operations

### ExecutionOrder Structure

The trading engine uses a unified `ExecutionOrder` format for all operations:

#### CEX Limit Order Example
```json
{
  "version": 1,
  "cl_id": "order_12345",
  "action": "place",
  "venue_type": "cex",
  "venue": "binance",
  "product_type": "spot",
  "details": {
    "symbol": "ETH/USDT",
    "side": "buy",
    "order_type": "limit",
    "time_in_force": "gtc",
    "size": 0.1,
    "price": 2000.0
  },
  "ts_ns": 1640995200000000000,
  "tags": {
    "strategy": "arbitrage",
    "session": "demo"
  }
}
```

#### AMM Swap Example
```json
{
  "version": 1,
  "cl_id": "swap_67890",
  "action": "place",
  "venue_type": "dex",
  "venue": "uniswap_v2",
  "product_type": "amm_swap",
  "details": {
    "chain": "ethereum",
    "protocol": "uniswap_v2",
    "token_in": "ETH",
    "token_out": "USDC",
    "trade_mode": "exact_in",
    "amount_in": 1.0,
    "slippage_bps": 50,
    "deadline_sec": 300,
    "recipient": "0x742D35Cc6681C0532"
  },
  "ts_ns": 1640995200000000000
}
```

#### CLMM Swap Example (Uniswap V3)
```json
{
  "version": 1,
  "cl_id": "clmm_54321",
  "action": "place",
  "venue_type": "dex",
  "venue": "uniswap_v3",
  "product_type": "clmm_swap",
  "details": {
    "chain": "ethereum",
    "protocol": "uniswap_v3",
    "pool": {
      "token0": "USDC",
      "token1": "ETH",
      "fee_tier_bps": 3000
    },
    "trade_mode": "exact_in",
    "amount_in": 1000.0,
    "slippage_bps": 30,
    "price_limit": 2100.0,
    "deadline_sec": 600,
    "recipient": "0x742D35Cc6681C0532"
  }
}
```

## 🏗️ Architecture

```
┌─────────────────┐   ExecutionOrder   ┌──────────────────────┐   ccapi/REST   ┌─────────────┐
│   Trading       │   PUSH->PULL       │  Trading Engine      │◄──────────────►│ CEX Markets │
│   Strategies    │   tcp://5601       │  Service             │                │ (Binance,   │
└─────────────────┘                    │                      │                │  Bybit...)  │
                                       │  ┌─────────────────┐ │                └─────────────┘
┌─────────────────┐   Reports/Fills    │  │ Order Processor │ │   Gateway API  ┌─────────────┐
│   Strategy      │◄──PUB->SUB────────┤  │ Market Data     │ │◄──────────────►│ DEX Markets │
│   Monitoring    │   tcp://5602       │  │ Backtest Engine │ │   (REST)       │ (Uniswap,   │
└─────────────────┘                    │  └─────────────────┘ │                │  Sushi...)  │
                                       │           ▲          │                └─────────────┘
┌─────────────────┐   Market Data      │           │          │   Blockchain   ┌─────────────┐
│   Market Data   │   SUB->PUB         │  ┌─────────────────┐ │◄──────────────►│ On-Chain    │
│   Pipeline      │   tcp://5556/5557  │  │ Market State    │ │   Transactions │ Networks    │
└─────────────────┘                    │  │ Tracking        │ │                │ (Ethereum,  │
                                       │  └─────────────────┘ │                │  BSC...)    │
                                       └──────────────────────┘                └─────────────┘
```

### Key Components

- **Order Processing**: Unified ExecutionOrder handling for all venue types
- **Market Data Integration**: Real-time preprocessing and state tracking
- **Multi-Venue Execution**: CEX (ccapi), DEX (Hummingbot Gateway), On-chain (direct)
- **Backtest Engine**: Realistic simulation using live market conditions
- **Risk Management**: Duplicate detection, validation, and order lifecycle tracking

## 🛠️ Build Instructions

### Prerequisites

#### System Dependencies (Ubuntu/WSL)
```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build build-essential pkg-config git
```

#### Clone Repository with Submodules
```bash
git clone https://github.com/Jss-on/latentspeed.git
cd latentspeed
git submodule update --init --recursive
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
./build.sh --debug

# Release build  
./build.sh --release
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
[TradingEngine] Initialized successfully
[TradingEngine] Order receiver endpoint: tcp://127.0.0.1:5601
[TradingEngine] Report publisher endpoint: tcp://127.0.0.1:5602
[TradingEngine] DEX Gateway URL: http://localhost:8080
[TradingEngine] Service started
[TradingEngine] Order receiver thread started
[TradingEngine] Publisher thread started
[TradingEngine] Trade subscriber thread started
[TradingEngine] Orderbook subscriber thread started
[TradingEngine] Backtest mode: enabled
[Main] Trading engine started successfully
[Main] Listening for orders on tcp://127.0.0.1:5601
[Main] Publishing reports on tcp://127.0.0.1:5602
[Main] Press Ctrl+C to stop
```

### Service Configuration

**Default Endpoints:**
- **Order Reception**: `tcp://127.0.0.1:5601` (PULL socket for ExecutionOrders)
- **Report Publishing**: `tcp://127.0.0.1:5602` (PUB socket for ExecutionReports/Fills)
- **Trade Data**: `tcp://127.0.0.1:5556` (SUB socket for preprocessed trades)
- **Orderbook Data**: `tcp://127.0.0.1:5557` (SUB socket for preprocessed orderbook)
- **Hummingbot Gateway**: `http://localhost:8080` (REST API for DEX operations)

**Default Settings:**
- **Backtest Mode**: Enabled (80% fill probability, 1 bps slippage)
- **Market Data Host**: `127.0.0.1` (configurable)
- **Fill Simulation**: Realistic based on market conditions

## 🧪 Testing the Service

### Prerequisites for Testing

1. **Start Market Data Pipeline** (if available):
   ```bash
   # Market data feeds should be running on:
   # tcp://127.0.0.1:5556 (preprocessed trades)
   # tcp://127.0.0.1:5557 (preprocessed orderbook)
   ```

2. **Start Hummingbot Gateway** (for DEX operations):
   ```bash
   # Run Hummingbot Gateway on http://localhost:8080
   # See Hummingbot documentation for setup
   ```

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
    "venue": "binance",
    "product_type": "spot",
    "details": {
        "symbol": "ETH/USDT",
        "side": "buy",
        "order_type": "limit",
        "time_in_force": "gtc",
        "size": 0.01,
        "price": 2000.0
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

#### AMM Swap Test
```python
import zmq
import json
import time

context = zmq.Context()
socket = context.socket(zmq.PUSH)
socket.connect("tcp://localhost:5601")

# Uniswap V2 ETH->USDC swap
swap_order = {
    "version": 1,
    "cl_id": f"swap_{int(time.time())}",
    "action": "place",
    "venue_type": "dex",
    "venue": "uniswap_v2",
    "product_type": "amm_swap",
    "details": {
        "chain": "ethereum",
        "protocol": "uniswap_v2",
        "token_in": "ETH",
        "token_out": "USDC",
        "trade_mode": "exact_in",
        "amount_in": 0.1,
        "slippage_bps": 50,
        "deadline_sec": 300,
        "recipient": "0x742D35Cc6681C0532"
    },
    "ts_ns": int(time.time() * 1e9)
}

socket.send_string(json.dumps(swap_order))
print(f"Sent swap order: {swap_order['cl_id']}")
```

### Testing in Backtest Mode

The engine runs in backtest mode by default with realistic fill simulation:

- **Fill Probability**: 80% (configurable)
- **Slippage**: 1 bps additional realistic slippage
- **Market Conditions**: Based on live orderbook data when available
- **Order Types**: Market orders fill immediately, limit orders fill when price is reached

## ⚙️ Configuration

### Service Configuration

The trading engine can be configured via constructor parameters or environment variables:

```cpp
// Default configuration in TradingEngineService constructor
order_endpoint_("tcp://127.0.0.1:5601")        // Order reception
report_endpoint_("tcp://127.0.0.1:5602")       // Report publishing  
gateway_base_url_("http://localhost:8080")     // Hummingbot Gateway
trade_endpoint_("tcp://127.0.0.1:5556")        // Preprocessed trades
orderbook_endpoint_("tcp://127.0.0.1:5557")    // Preprocessed orderbook
backtest_mode_(true)                            // Enable simulation
fill_probability_(0.8)                          // 80% fill rate
slippage_bps_(1.0)                             // 1 bps slippage
```

### Exchange Configuration (CEX)
Edit `CCAPI_COMPILE_DEFS` in `CMakePresets.json` to enable exchanges:
```json
"CCAPI_COMPILE_DEFS": "CCAPI_ENABLE_SERVICE_MARKET_DATA;CCAPI_ENABLE_SERVICE_EXECUTION_MANAGEMENT;CCAPI_ENABLE_EXCHANGE_BINANCE;CCAPI_ENABLE_EXCHANGE_BYBIT"
```

### DEX Configuration
Configure Hummingbot Gateway for DEX operations:
- **Gateway URL**: Default `http://localhost:8080`
- **Supported Protocols**: Uniswap V2/V3, SushiSwap, PancakeSwap
- **Supported Chains**: Ethereum, BSC, Polygon, Avalanche

### Backtest Configuration
Adjust simulation parameters:
- **Fill Probability**: `0.0` to `1.0` (default: `0.8`)
- **Slippage**: Basis points additional slippage (default: `1.0`)
- **Market Data**: Requires live orderbook/trade feeds for realistic fills

## 📦 Dependencies

Managed via vcpkg (`vcpkg.json`):
- **openssl**: Secure communications
- **boost-asio, boost-beast**: Networking and HTTP
- **rapidjson**: JSON parsing
- **zeromq**: Inter-process communication
- **zlib**: Compression (optional)
- **hffix**: FIX protocol support (optional)

## 🔧 Development

### Project Structure
```
latentspeed/
├── include/
│   └── trading_engine_service.h    # Service interface
├── src/
│   ├── trading_engine_service.cpp  # Service implementation  
│   └── main.cpp                    # Entry point
├── ccapi/                          # Crypto exchange API (submodule)
├── external/vcpkg/                 # Package manager (submodule)
├── CMakeLists.txt                  # Build configuration
├── CMakePresets.json               # Build presets
└── vcpkg.json                      # Dependencies manifest
```

### Adding New Exchanges
1. Add exchange macro to `CCAPI_COMPILE_DEFS` in `CMakePresets.json`
2. Rebuild the project
3. Use the exchange name in strategy commands

### Extending Functionality
- Implement additional command types in `handle_strategy_message()`
- Add new market data processing in `processEvent()`
- Extend the JSON protocol as needed

## 📝 License

[Add your license information here]

## 🤝 Contributing

[Add contribution guidelines here]