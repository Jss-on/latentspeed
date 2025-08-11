# Latentspeed Trading Engine

A high-performance cryptocurrency trading engine that provides real-time market data streaming and order execution capabilities. The engine uses ZeroMQ for communication and integrates with multiple cryptocurrency exchanges via the ccapi library.

![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-green.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)

## 🚀 Features

### Core Functionality
- **Real-time Market Data**: Subscribe to live market data from supported exchanges
- **Order Execution**: Execute buy/sell orders with various order types (LIMIT, MARKET)
- **Multi-Exchange Support**: Currently configured for OKX, easily extensible to other exchanges
- **Strategy Decoupling**: Strategy logic runs separately and communicates via ZeroMQ
- **High Performance**: Low-latency communication using ZeroMQ and direct exchange APIs

### Communication Architecture
- **ZeroMQ REP Socket** (`tcp://*:5555`): Receives strategy commands
- **ZeroMQ PUB Socket** (`tcp://*:5556`): Broadcasts market data updates
- **JSON Protocol**: Simple, structured command/response format

### Supported Commands

#### Place Order
```json
{
  "type": "PLACE_ORDER",
  "exchange": "okx",
  "instrument": "BTC-USDT",
  "side": "BUY",
  "quantity": 0.001,
  "price": 50000.0,
  "order_type": "LIMIT",
  "correlation_id": "optional_id"
}
```

#### Subscribe to Market Data
```json
{
  "type": "SUBSCRIBE_MARKET_DATA",
  "exchange": "okx",
  "instrument": "BTC-USDT"
}
```

## 🏗️ Architecture

```
┌─────────────────┐    ZeroMQ REQ     ┌──────────────────────┐    ccapi    ┌─────────────┐
│   Strategy      │◄─────────────────►│  Trading Engine      │◄───────────►│  Exchanges  │
│   Components    │    tcp://5555     │  Service             │             │  (OKX, etc) │
└─────────────────┘                   └──────────────────────┘             └─────────────┘
                                               │
                                               │ ZeroMQ PUB
                                               │ tcp://5556
                                               ▼
                                      ┌─────────────────┐
                                      │  Market Data    │
                                      │  Subscribers    │
                                      └─────────────────┘
```

## 🛠️ Build Instructions

### Prerequisites

#### System Dependencies (Ubuntu/WSL)
```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build build-essential pkg-config git
```

#### Clone Repository with Submodules
```bash
git clone <your-repo-url> latentspeed
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

#### Configure with CMake Presets
```bash
# Debug build
cmake --preset wsl-debug

# Release build  
cmake --preset wsl-release
```

#### Build the Project
```bash
# Debug
cmake --build --preset wsl-debug

# Release
cmake --build --preset wsl-release
```

#### Alternative Manual Configuration
If presets don't work, configure manually:
```bash
mkdir build && cd build
cmake -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=../external/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  ..
cmake --build .
```

## 🚀 Running the Trading Engine

### Start the Service
```bash
# From build directory
./trading_engine_service

# Or with full path
./build/wsl-release/trading_engine_service
```

### Expected Output
```
=== Latentspeed Trading Engine Service ===
Starting up...
[TradingEngine] Initialized successfully
[TradingEngine] Strategy endpoint: tcp://*:5555
[TradingEngine] Market data endpoint: tcp://*:5556
[TradingEngine] Service started
[Main] Trading engine started successfully
[Main] Listening for strategy commands on tcp://*:5555
[Main] Broadcasting market data on tcp://*:5556
[Main] Press Ctrl+C to stop
```

## 🧪 Testing the Service

### Using Python Client (Example)
```python
import zmq
import json
import time

# Connect to trading engine
context = zmq.Context()
socket = context.socket(zmq.REQ)
socket.connect("tcp://localhost:5555")

# Subscribe to market data
subscribe_cmd = {
    "type": "SUBSCRIBE_MARKET_DATA",
    "exchange": "okx",
    "instrument": "BTC-USDT"
}
socket.send_string(json.dumps(subscribe_cmd))
response = socket.recv_string()
print(f"Subscription response: {response}")

# Place a test order
order_cmd = {
    "type": "PLACE_ORDER",
    "exchange": "okx",
    "instrument": "BTC-USDT", 
    "side": "BUY",
    "quantity": 0.001,
    "price": 45000.0,
    "order_type": "LIMIT"
}
socket.send_string(json.dumps(order_cmd))
response = socket.recv_string()
print(f"Order response: {response}")
```

### Market Data Subscriber (Example)
```python
import zmq

context = zmq.Context()
socket = context.socket(zmq.SUB)
socket.connect("tcp://localhost:5556")
socket.setsockopt(zmq.SUBSCRIBE, b"")  # Subscribe to all messages

while True:
    message = socket.recv_string()
    print(f"Market data: {message}")
```

## ⚙️ Configuration

### Exchange Configuration
Edit `CCAPI_COMPILE_DEFS` in `CMakePresets.json` to enable additional exchanges:
```json
"CCAPI_COMPILE_DEFS": "CCAPI_ENABLE_SERVICE_MARKET_DATA;CCAPI_ENABLE_SERVICE_EXECUTION_MANAGEMENT;CCAPI_ENABLE_EXCHANGE_OKX;CCAPI_ENABLE_EXCHANGE_BINANCE"
```

### Available Exchanges
- OKX (default)
- Binance
- Coinbase
- Kraken
- And many more (see ccapi documentation)

### Optional Features
- **ZLIB Support**: Set `LATENTSPEED_WITH_ZLIB=ON` for Huobi/Bitmart exchanges
- **FIX API**: Add `CCAPI_ENABLE_SERVICE_FIX` for FIX protocol support

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