# LatentSpeed Trading System

Ultra-low latency C++ trading system with native exchange connectivity, real-time market data preprocessing, and high-performance order execution. Built for production algorithmic trading with microsecond-level performance.

![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-green.svg)
![ZeroMQ](https://img.shields.io/badge/ZeroMQ-4.3%2B-red.svg)
![Production](https://img.shields.io/badge/Status-Production-green.svg)

## 🏗️ Production Architecture

LatentSpeed runs as **two independent production executables**:

```
┌──────────────────┐         ZMQ (5556/5557)         ┌─────────────────────┐
│  marketstream    │────────────────────────────────►│ trading_engine_     │
│                  │    Preprocessed Market Data     │ service             │
└──────────────────┘                                 └─────────────────────┘
   │                                                          │
   │ WebSocket                                                │ REST API
   ▼                                                          ▼
┌──────────────────┐                                 ┌─────────────────────┐
│  Exchanges       │                                 │  Exchanges          │
│  (dYdX, Bybit)   │                                 │  (Bybit)            │
│  Market Data     │                                 │  Order Execution    │
└──────────────────┘                                 └─────────────────────┘
```

### 1. **marketstream** - Market Data Provider
**Location:** `src/marketstream.cpp`  
**Purpose:** Connect to exchanges, preprocess data, stream via ZMQ

- ✅ WebSocket connections to multiple exchanges (dYdX, Bybit)
- ✅ Real-time market microstructure feature calculation
- ✅ ZMQ publishing (trades on 5556, orderbooks on 5557)
- ✅ YAML configuration for dynamic symbol management
- ✅ ~10-50μs latency (10x faster than Python)

**Features Calculated:**
- **Trades**: VWAP, volume, rolling volatility
- **Orderbooks**: Midpoint, spread, imbalance, depth, OFI, breadth

### 2. **trading_engine_service** - Trading Engine
**Location:** `src/main.cpp`  
**Purpose:** Execute strategies, manage risk, place orders

- ✅ Native Bybit REST API client for order execution
- ✅ HMAC authentication and request signing
- ✅ Order lifecycle management (place/cancel/modify)
- ✅ Real-time execution reports and fills
- ✅ WebSocket order updates
- ✅ Multi-threaded async architecture

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

## 🚀 Features

### Market Data Pipeline (marketstream)
- **Multi-Exchange Support**: dYdX V4, Bybit (extensible to Binance, etc.)
- **Real-Time Preprocessing**: Market microstructure features calculated in-stream
- **YAML Configuration**: Dynamic symbol management without recompilation
- **Ultra-Low Latency**: Native C++ WebSocket, ~10-50μs processing time
- **ZMQ Streaming**: Separate ports for trades (5556) and orderbooks (5557)

### Order Execution (trading_engine_service)
- **Exchange Connectivity**: Native Bybit REST API client
- **Order Types**: Market and limit orders with TIF options
- **Order Management**: Place, cancel, modify with client order ID tracking
- **Real-Time Updates**: WebSocket streams for order status and fills
- **HMAC Authentication**: Secure API request signing
- **Production Ready**: Testnet and mainnet support

## ⚡ Quick Start

### 1. Build
```bash
# Clone repository
git clone https://github.com/Jss-on/latentspeed.git
cd latentspeed

# Build (automatically installs dependencies via vcpkg)
./run.sh --release
```

### 2. Configure
Edit `config.yml`:
```yaml
zmq:
  port: 5556  # trades, books on 5557
  window_size: 20

feeds:
  - exchange: dydx
    symbols:
      - BTC-USD
      - ETH-USD
```

### 3. Run Production Stack
```bash
# Terminal 1 - Start market data provider
./build/linux-release/marketstream config.yml

# Terminal 2 - Start trading engine
./build/linux-release/trading_engine_service \
  --exchange bybit \
  --api-key YOUR_KEY \
  --api-secret YOUR_SECRET
```

### 4. Monitor
```python
# Python ZMQ subscriber example
import zmq, json

ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect("tcp://127.0.0.1:5556")  # Trades
sock.subscribe(b"")

while True:
    msg = sock.recv_string()
    print(json.loads(msg))
```

## 🛠️ Build Instructions (Detailed)

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

### Build Script

The `run.sh` script handles all build automation:

```bash
# Release build (production)
./run.sh --release

# Debug build (development)
./run.sh --debug

# Clean rebuild
./run.sh --release --clean
```

**Output:**
```
build/
├── linux-release/
│   ├── marketstream              # Market data provider
│   └── trading_engine_service    # Trading engine
└── linux-debug/                  # Debug builds
```

## 🎯 Configuration

### MarketStream Config (`config.yml`)

```yaml
# ZMQ output configuration
zmq:
  enabled: true
  host: 127.0.0.1
  port: 5556  # trades port, books will use port+1 (5557)
  window_size: 20
  depth_levels: 10

# Logging
log:
  filename: marketstream.log
  level: info  # trace, debug, info, warn, error, critical

# Exchange feeds
feeds:
  - exchange: dydx
    symbols:
      - BTC-USD
      - ETH-USD
      - SOL-USD
    enable_trades: true
    enable_orderbook: true
    snapshots_only: true

  - exchange: bybit
    symbols:
      - BTC-USDT
      - ETH-USDT
    enable_trades: true
    enable_orderbook: true
    snapshots_only: true
```

**Dynamic Symbol Management:** Edit config.yml and restart - no recompilation needed!

### Trading Engine CLI

```bash
./trading_engine_service \
  --exchange bybit \
  --api-key YOUR_API_KEY \
  --api-secret YOUR_API_SECRET \
  --live-trade  # Optional: enable mainnet (default is testnet)
```

## 🚀 Running Production Stack

### 1. Start MarketStream

```bash
./build/linux-release/marketstream config.yml
```

**Expected Output:**
```
[2025-10-15 18:00:00.123] [marketstream] [info] ===========================================
[2025-10-15 18:00:00.123] [marketstream] [info] LatentSpeed MarketStream
[2025-10-15 18:00:00.123] [marketstream] [info] Production Market Data Provider (C++)
[2025-10-15 18:00:00.123] [marketstream] [info] Config: config.yml
[2025-10-15 18:00:00.123] [marketstream] [info] ===========================================
[2025-10-15 18:00:00.124] [marketstream] [info] Adding dydx feed: 5 symbols
[2025-10-15 18:00:00.124] [marketstream] [info] Starting 1 feed(s) with 5 total symbols...
[2025-10-15 18:00:00.125] [marketstream] [info] ===========================================
[2025-10-15 18:00:00.125] [marketstream] [info] Streaming market data (Press Ctrl+C to stop)
[2025-10-15 18:00:00.125] [marketstream] [info] ZMQ Output:
[2025-10-15 18:00:00.125] [marketstream] [info]   - Trades:     tcp://127.0.0.1:5556
[2025-10-15 18:00:00.125] [marketstream] [info]   - Orderbooks: tcp://127.0.0.1:5557
[2025-10-15 18:00:00.125] [marketstream] [info] ===========================================
```

### 2. Start Trading Engine

```bash
./build/linux-release/trading_engine_service \
  --exchange bybit \
  --api-key YOUR_API_KEY \
  --api-secret YOUR_API_SECRET
```

**Expected Output:**
```
[2025-10-15 18:00:10.123] [Main] Configuration Summary:
[2025-10-15 18:00:10.123] [Main]   Exchange: bybit
[2025-10-15 18:00:10.123] [Main]   Trading Mode: DEMO/TESTNET
[2025-10-15 18:00:10.124] [TradingEngine] Creating ZeroMQ context...
[2025-10-15 18:00:10.124] [TradingEngine] Order receiver socket bound to tcp://127.0.0.1:5601
[2025-10-15 18:00:10.125] [TradingEngine] Report publisher socket bound to tcp://127.0.0.1:5602
[2025-10-15 18:00:10.126] [TradingEngine] Initializing Bybit client...
[2025-10-15 18:00:10.127] [Main] Trading engine started successfully
[2025-10-15 18:00:10.127] [Main] Listening for orders on tcp://127.0.0.1:5601
[2025-10-15 18:00:10.127] [Main] Publishing reports on tcp://127.0.0.1:5602
```

### 3. ZMQ Endpoints

**MarketStream Output (market data):**
- `tcp://127.0.0.1:5556` - Preprocessed trades
- `tcp://127.0.0.1:5557` - Preprocessed orderbooks

**Trading Engine I/O (order execution):**
- `tcp://127.0.0.1:5601` - PULL socket for ExecutionOrders
- `tcp://127.0.0.1:5602` - PUB socket for ExecutionReports/Fills

## 🧪 Testing

### Subscribe to Market Data (ZMQ)

```python
import zmq
import json

# Subscribe to trades
ctx = zmq.Context()
trades = ctx.socket(zmq.SUB)
trades.connect("tcp://127.0.0.1:5556")
trades.subscribe(b"")

while True:
    msg = trades.recv_string()
    data = json.loads(msg)
    print(f"Trade: {data['symbol']} @ ${data['price']}")
```

### Send ExecutionOrder (PUSH socket)
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

## 🔒 Production Deployment

See [PRODUCTION.md](PRODUCTION.md) for complete deployment guide including:
- Systemd service configuration
- CPU affinity and real-time priority
- Network tuning for low latency
- Monitoring and logging
- Multi-environment configs

## 📊 Data Formats

### Market Data (ZMQ)

**Trade Message (Port 5556):**
```json
{
  "exchange": "DYDX",
  "symbol": "BTC-USD",
  "timestamp_ns": 1640995200123456789,
  "receipt_timestamp_ns": 1640995200123456890,
  "price": 50000.50,
  "amount": 0.5,
  "side": "buy",
  "seq": 12345,
  "transaction_price": 50000.50,
  "trading_volume": 25000.25,
  "volatility": 0.0023
}
```

**Orderbook Message (Port 5557):**
```json
{
  "exchange": "DYDX",
  "symbol": "BTC-USD",
  "timestamp_ns": 1640995200123456789,
  "seq": 12346,
  "bids": [[50000.0, 1.5], [49999.5, 2.0]],
  "asks": [[50000.5, 1.2], [50001.0, 1.8]],
  "midpoint": 50000.25,
  "relative_spread": 0.00001,
  "imbalance_lvl1": 0.2,
  "bid_depth_n": 250000.50,
  "ask_depth_n": 240000.30,
  "volatility_mid": 0.0018,
  "ofi_rolling": 0.15
}
```

### Order Execution (ZMQ)

See [Order Message Formats](#order-message-formats) section above.

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
├── src/
│   ├── marketstream.cpp            # Production: Market data provider
│   ├── main.cpp                    # Production: Trading engine entry
│   ├── trading_engine_service.cpp  # Trading engine implementation
│   ├── market_data_provider.cpp    # Market data pipeline
│   ├── exchange_interface.cpp      # Exchange parsers (dYdX, Bybit)
│   ├── feed_handler.cpp            # Multi-feed coordinator
│   └── exchange/
│       ├── bybit_client.cpp        # Bybit REST API client
│       └── exchange_client.cpp     # Base exchange client
├── include/
│   ├── trading_engine_service.h
│   ├── market_data_provider.h
│   ├── exchange_interface.h
│   ├── feed_handler.h
│   └── exchange/
│       ├── bybit_client.h
│       └── exchange_client.h
├── config.yml                      # MarketStream configuration
├── CMakeLists.txt                  # Build configuration
├── run.sh                          # Build automation script
├── PRODUCTION.md                   # Production deployment guide
└── vcpkg.json                      # Dependencies
```

### Adding Exchange Support

**Market Data (marketstream):**
1. Add exchange implementation to `exchange_interface.cpp`
2. Implement `generate_subscription()` and `parse_message()`
3. Add to exchange factory in `feed_handler.cpp`
4. Update `config.yml` with new exchange

**Order Execution (trading_engine_service):**
1. Create new class inheriting from `ExchangeClient`
2. Implement REST API and WebSocket handlers
3. Add HMAC authentication for the exchange
4. Register in `TradingEngineService::initialize()`

## 📝 License

[Add your license information here]

## 🤝 Contributing

[Add contribution guidelines here]