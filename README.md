# Latentspeed Trading Engine

A high-performance C++ trading engine for algorithmic trading with real-time market data processing, multi-exchange connectivity, and comprehensive order execution capabilities. Features runtime configuration, connection validation, and preprocessed market data publishing.

![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-green.svg)
![ZeroMQ](https://img.shields.io/badge/ZeroMQ-4.3%2B-red.svg)
![CCAPI](https://img.shields.io/badge/CCAPI-latest-orange.svg)
![spdlog](https://img.shields.io/badge/spdlog-1.x-yellow.svg)

## 🚀 Features

### Real-Time Market Data Processing
- **Multi-Exchange Connectivity**: OKX, Binance, Coinbase, Kraken via CCAPI
- **Preprocessed Data Streams**: Trade and orderbook data with rolling statistics
- **Connection Validation**: Pre-startup exchange connectivity testing
- **Runtime Configuration**: Environment variable-based exchange/symbol selection
- **Market Data Publishing**: Separate ZMQ endpoints for trades (port 5558) and orderbooks (port 5559)

### Advanced Market Data Features
- **Rolling Statistics**: Volatility, OFI (Order Flow Imbalance), midpoint tracking
- **FastRollingStats**: Efficient sliding window calculations
- **Sequence Numbering**: Per-stream message sequencing
- **Symbol Normalization**: Consistent symbol formatting across exchanges
- **Nanosecond Timestamps**: High-precision timing for all market events

### Trading Engine Core
- **Order Execution**: CEX spot and perpetual order handling
- **Backtest Simulation**: Realistic fill simulation with configurable slippage
- **Risk Management**: Duplicate order detection and validation
- **Multi-threaded Architecture**: Separate threads for order processing and market data

### Communication Architecture
- **Order Reception**: ZeroMQ PULL socket (`tcp://127.0.0.1:5601`) for ExecutionOrders
- **Report Publishing**: ZeroMQ PUB socket (`tcp://127.0.0.1:5602`) for ExecutionReports and Fills
- **Trade Data**: ZeroMQ PUB socket (`tcp://127.0.0.1:5558`) for preprocessed trades
- **Orderbook Data**: ZeroMQ PUB socket (`tcp://127.0.0.1:5559`) for preprocessed orderbooks
- **Structured Logging**: spdlog-based logging with configurable levels

### Market Data Structures

#### Trade Data Format
```json
{
  "exchange": "OKX",
  "symbol": "BTC-USDT",
  "timestamp_ns": 1640995200000000000,
  "receipt_timestamp_ns": 1640995200000001000,
  "price": 50000.0,
  "amount": 0.1,
  "side": "buy",
  "trade_id": "12345",
  "trading_volume": 5000.0,
  "volatility_transaction_price": 0.025,
  "transaction_price_window_size": 20,
  "sequence_number": 1001
}
```

#### Orderbook Data Format
```json
{
  "exchange": "OKX",
  "symbol": "BTC-USDT",
  "timestamp_ns": 1640995200000000000,
  "receipt_timestamp_ns": 1640995200000001000,
  "best_bid_price": 49995.0,
  "best_bid_size": 0.5,
  "best_ask_price": 50005.0,
  "best_ask_size": 0.3,
  "midpoint": 50000.0,
  "relative_spread": 0.0002,
  "imbalance_lvl1": 0.25,
  "volatility_mid": 0.015,
  "ofi_rolling": 0.1,
  "sequence_number": 2001
}
```

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
┌─────────────────┐   ExecutionOrder   ┌──────────────────────┐   CCAPI WebSocket ┌─────────────┐
│   Trading       │   PUSH->PULL       │  Trading Engine      │◄─────────────────►│ CEX Markets │
│   Strategies    │   tcp://5601       │  Service             │                   │ OKX, Binance│
└─────────────────┘                    │                      │                   │ Coinbase... │
                                       │  ┌─────────────────┐ │                   └─────────────┘
┌─────────────────┐   Reports/Fills    │  │ Order Processor │ │                   
│   Strategy      │◄──PUB->SUB────────┤  │ Market Data     │ │   Connection      ┌─────────────┐
│   Monitoring    │   tcp://5602       │  │ Preprocessor    │ │   Validation      │ Connectivity│
└─────────────────┘                    │  │ FastRollingStats│ │◄─────────────────►│ Checker     │
                                       │  └─────────────────┘ │                   └─────────────┘
┌─────────────────┐   Preprocessed     │           ▲          │                   
│   Market Data   │   Trade Data       │           │          │   Runtime Config  ┌─────────────┐
│   Subscribers   │◄──PUB->SUB────────┤  ┌─────────────────┐ │◄─────────────────►│ Environment │
│                 │   tcp://5558       │  │ Market State    │ │   EXCHANGES=...   │ Variables   │
└─────────────────┘                    │  │ Tracking        │ │   SYMBOLS=...     └─────────────┘
┌─────────────────┐   Preprocessed     │  └─────────────────┘ │                   
│   Orderbook     │   Book Data        │                      │                   
│   Subscribers   │◄──PUB->SUB────────┤                      │                   
│                 │   tcp://5559       │                      │                   
└─────────────────┘                    └──────────────────────┘                   
```

### Key Components

- **Market Data Engine**: Real-time CCAPI integration with preprocessing and rolling statistics
- **Connection Validation**: Pre-startup connectivity testing for all configured exchanges
- **Runtime Configuration**: Environment variable-based exchange and symbol selection
- **Order Processing**: CEX spot and perpetual order execution with backtest simulation
- **Multi-threaded Architecture**: Separate threads for orders, market data, and publishing
- **Structured Logging**: spdlog-based logging with timestamps and context
- **FastRollingStats**: Efficient sliding window calculations for volatility and OFI

## 🛠️ Build Instructions

### Prerequisites

#### System Dependencies (Ubuntu/WSL)
```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build build-essential pkg-config git python3-dev python3-pip
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
./run.sh --debug

# Release build  
./run.sh --release

# Build with Python bindings (default enabled)
./run.sh --release
```

### Python Bindings Build

The Python bindings are built automatically when `LATENTSPEED_WITH_PYTHON=ON` (default). After building:

```bash
# Install the Python module (from build directory)
cd build
pip install --user .

# Or use the module directly from build directory
export PYTHONPATH="$PWD:$PYTHONPATH"
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

## 🐍 Python Bindings

### Overview

The latentspeed Python bindings provide direct access to the C++ trading engine from Python, enabling:
- **Direct C++ Integration**: Access TradingEngineService directly without ZMQ overhead
- **Market Data Structures**: Native Python access to TradeData, OrderBookData, etc.
- **High Performance**: C++ speed with Python convenience
- **Rolling Statistics**: FastRollingStats for real-time calculations

### Python API Usage

#### Basic Trading Engine Control

```python
import latentspeed

# Create and start trading engine
engine = latentspeed.TradingEngineService()

# Initialize and start
if engine.initialize():
    engine.start()
    print("Trading engine started successfully")
    
    # Check status
    print(f"Running: {engine.is_running()}")
    
    # Stop when done
    engine.stop()
```

#### Working with Market Data Structures

```python
import latentspeed

# Create trade data
trade = latentspeed.TradeData()
trade.exchange = "BYBIT"
trade.symbol = "BTC-USDT"
trade.price = 50000.0
trade.amount = 0.1
trade.side = "buy"
trade.timestamp_ns = 1640995200000000000
trade.trade_id = "12345"

# Access computed fields
print(f"Trading volume: {trade.trading_volume}")
print(f"Volatility: {trade.volatility_transaction_price}")

# Create orderbook data
book = latentspeed.OrderBookData()
book.exchange = "BYBIT"
book.symbol = "BTC-USDT"
book.best_bid_price = 49995.0
book.best_bid_size = 0.5
book.best_ask_price = 50005.0
book.best_ask_size = 0.3
book.timestamp_ns = 1640995200000000000

# Access derived metrics
print(f"Midpoint: {book.midpoint}")
print(f"Spread: {book.relative_spread}")
print(f"Imbalance: {book.imbalance_lvl1}")
```

#### Rolling Statistics

```python
import latentspeed

# Create rolling stats calculator
stats = latentspeed.FastRollingStats(window_size=20)

# Process trade data
for trade_price in [50000, 50100, 49900, 50200]:
    result = stats.update_trade(trade_price)
    print(f"Volatility: {result.volatility_transaction_price}")
    print(f"Window size: {result.transaction_price_window_size}")

# Process orderbook data
book_result = stats.update_book(
    midpoint=50050.0,
    best_bid_price=50000.0, best_bid_size=0.5,
    best_ask_price=50100.0, best_ask_size=0.3
)
print(f"Mid volatility: {book_result.volatility_mid}")
print(f"OFI: {book_result.ofi_rolling}")
```

#### Order Structures

```python
import latentspeed
import time

# Create execution order
order = latentspeed.ExecutionOrder()
order.version = 1
order.cl_id = f"py_order_{int(time.time())}"
order.action = "place"
order.venue_type = "cex"
order.venue = "bybit"
order.product_type = "spot"
order.ts_ns = int(time.time() * 1e9)

# Set order details
order.details["symbol"] = "BTC-USDT"
order.details["side"] = "buy"
order.details["order_type"] = "limit"
order.details["size"] = "0.1"
order.details["price"] = "50000.0"

# Set tags
order.tags["strategy"] = "python_test"
order.tags["session"] = "demo"

print(f"Created order: {order.cl_id}")
```

### ZMQ Integration with Python Bindings

You can combine the Python bindings with ZMQ for hybrid approaches:

```python
import latentspeed
import zmq
import json
import threading
import time

class HybridTradingSystem:
    def __init__(self):
        # Direct C++ engine access
        self.engine = latentspeed.TradingEngineService()
        
        # ZMQ for external communication
        self.context = zmq.Context()
        self.order_socket = self.context.socket(zmq.PUSH)
        self.report_socket = self.context.socket(zmq.SUB)
        
    def start(self):
        # Start C++ engine
        if self.engine.initialize():
            self.engine.start()
            
        # Connect ZMQ sockets
        self.order_socket.connect("tcp://localhost:5601")
        self.report_socket.connect("tcp://localhost:5602")
        self.report_socket.setsockopt(zmq.SUBSCRIBE, b"")
        
        # Start report monitoring thread
        self.monitor_thread = threading.Thread(target=self._monitor_reports)
        self.monitor_thread.daemon = True
        self.monitor_thread.start()
        
    def send_order_via_zmq(self, order_dict):
        """Send order via ZMQ (external interface)"""
        self.order_socket.send_string(json.dumps(order_dict))
        
    def create_order_direct(self):
        """Create order directly via Python bindings"""
        order = latentspeed.ExecutionOrder()
        order.cl_id = f"direct_{int(time.time())}"
        order.action = "place"
        # ... configure order
        return order
        
    def _monitor_reports(self):
        """Monitor execution reports"""
        while True:
            try:
                message = self.report_socket.recv_string(zmq.NOBLOCK)
                report = json.loads(message)
                print(f"Report: {report['cl_id']} -> {report['status']}")
            except zmq.Again:
                time.sleep(0.01)
                
    def stop(self):
        self.engine.stop()
        self.context.term()

# Usage
system = HybridTradingSystem()
system.start()

# Use both interfaces
order_dict = {
    "cl_id": "zmq_order_1",
    "action": "place",
    "venue_type": "cex"
    # ... more fields
}
system.send_order_via_zmq(order_dict)

direct_order = system.create_order_direct()
# Process direct_order as needed
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