# Building Multi-Exchange Provider (Bybit + Binance + dYdX)

## ✅ CMakeLists.txt Updated

The following changes have been made to support multi-exchange functionality:

### New Source Files Added:
- ✅ `src/exchange_interface.cpp` - Bybit, Binance, dYdX implementations
- ✅ `src/feed_handler.cpp` - Multi-exchange feed manager
- ✅ `examples/multi_exchange_provider.cpp` - Example program

### New Build Targets:
1. **`trading_engine_service`** - Now includes exchange interface support
2. **`test_market_data`** - Now includes exchange interface support
3. **`multi_exchange_provider`** - New example for testing all 3 exchanges

---

## Build Instructions

### Option 1: Using run.sh (Recommended)

```bash
cd /home/tensor/latentspeed

# Clean previous build
rm -rf build/release

# Build release version
./run.sh --release
```

### Option 2: Manual Build

```bash
cd /home/tensor/latentspeed

# Create build directory
mkdir -p build/release
cd build/release

# Configure with CMake
cmake ../.. -DCMAKE_BUILD_TYPE=Release

# Build all targets
ninja

# Or build specific target
ninja multi_exchange_provider
```

---

## Running the Multi-Exchange Provider

### Test with All 3 Exchanges

```bash
cd /home/tensor/latentspeed/build/release

# Run multi-exchange example (programmatic config)
./multi_exchange_provider

# Expected output:
# [INFO] ============================================================
# [INFO] Multi-Exchange Market Data Provider
# [INFO] Similar to Python cryptofeed FeedHandler
# [INFO] ============================================================
# [INFO] Using programmatic configuration
# [INFO] Added 3 feeds
# [INFO] Starting feed handler...
# [INFO] [FeedHandler] Starting feed: bybit
# [INFO] [FeedHandler] Starting feed: binance
# [INFO] [FeedHandler] Starting feed: dydx
# [INFO] ============================================================
# [INFO] Feed handler running. Press Ctrl+C to stop.
# [INFO] ZMQ Ports: 5556 (trades), 5557 (orderbooks)
# [INFO] ============================================================
```

### Test with Config File

```bash
# Create config.yml first
cat > /home/tensor/latentspeed/config.yml << 'EOF'
zmq:
  port: 5556
  window_size: 20

feeds:
  - exchange: bybit
    symbols:
      - BTC-USDT
      - ETH-USDT
    snapshots_only: true
    
  - exchange: binance
    symbols:
      - BTC-USDT
      - ETH-USDT
    snapshots_only: true
    
  - exchange: dydx
    symbols:
      - BTC-USD
      - ETH-USD
    snapshots_only: true
EOF

# Run with config
./multi_exchange_provider --config ../../config.yml
```

---

## Verify ZMQ Messages

Open a **separate terminal** and subscribe to messages:

### Subscribe to All Trades
```bash
python3 << 'EOF'
import zmq
ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect("tcp://127.0.0.1:5556")
sock.setsockopt_string(zmq.SUBSCRIBE, "")  # All trades

while True:
    topic = sock.recv_string()
    message = sock.recv_string()
    print(f"[{topic}] {message[:100]}...")
EOF
```

### Subscribe to dYdX Only
```bash
python3 << 'EOF'
import zmq
ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect("tcp://127.0.0.1:5556")
sock.setsockopt_string(zmq.SUBSCRIBE, "DYDX-")  # dYdX only

while True:
    topic = sock.recv_string()
    message = sock.recv_string()
    print(f"[{topic}]")
    print(message)
    print("-" * 60)
EOF
```

### Subscribe to All Orderbooks
```bash
python3 << 'EOF'
import zmq
ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect("tcp://127.0.0.1:5557")
sock.setsockopt_string(zmq.SUBSCRIBE, "")  # All orderbooks

while True:
    topic = sock.recv_string()
    message = sock.recv_string()
    print(f"[{topic}] {message[:100]}...")
EOF
```

---

## Expected Behavior

### Successful Startup
```
[INFO] ============================================================
[INFO] Multi-Exchange Market Data Provider
[INFO] ============================================================
[INFO] Added 3 feeds
[INFO] Starting feed handler...
[INFO] [MarketData] Initializing provider for exchange: bybit
[INFO] [MarketData] Initializing provider for exchange: binance
[INFO] [MarketData] Initializing provider for exchange: dydx
[INFO] [MarketData] Connecting to WebSocket: wss://stream.bybit.com:443/v5/public/spot
[INFO] [MarketData] Connecting to WebSocket: wss://stream.binance.com:9443/ws
[INFO] [MarketData] Connecting to WebSocket: wss://indexer.dydx.trade:443/v4/ws
[INFO] [MarketData] WebSocket connected successfully
[INFO] [MarketData] Subscription message: {...}
[INFO] [MarketData] Subscription sent successfully
```

### Live Messages
```
[TRADE] BYBIT BTC-USDT @ 65432.50 x 0.00150000 buy
[TRADE] BINANCE ETH-USDT @ 3245.67 x 0.25000000 sell
[TRADE] DYDX BTC-USD @ 65433.00 x 0.10000000 buy
[BOOK] BYBIT BTC-USDT - Mid: 65432.75 Spread: 0.0015% Vol: 0.0023 OFI: 0.1234
[BOOK] DYDX ETH-USD - Mid: 3245.50 Spread: 0.0012% Vol: 0.0018 OFI: -0.0567
```

### Statistics (every 10 seconds)
```
[INFO] --- Statistics Report ---
[INFO] bybit: 1523 msgs received, 1523 published, 0 errors
[INFO] binance: 987 msgs received, 987 published, 0 errors
[INFO] dydx: 654 msgs received, 654 published, 0 errors
[INFO] ------------------------
```

---

## Troubleshooting

### Issue: Build fails with "undefined reference to DydxExchange"

**Cause**: CMakeLists.txt not updated with new source files

**Solution**: 
```bash
# Verify exchange_interface.cpp is in CMakeLists.txt
grep "exchange_interface.cpp" CMakeLists.txt

# Should show:
#   src/exchange_interface.cpp
```

### Issue: multi_exchange_provider not found

**Cause**: Target not built

**Solution**:
```bash
cd build/release
ninja multi_exchange_provider
```

### Issue: "No such file or directory" for feed_handler.h

**Cause**: Include path not set

**Solution**: Already fixed in CMakeLists.txt with:
```cmake
target_include_directories(multi_exchange_provider PRIVATE include)
```

### Issue: dYdX not receiving data

**Cause**: dYdX uses individual subscription messages

**Check**: Look for these log messages:
```
[INFO] [MarketData] Sent dYdX subscription (78 bytes): {"type":"subscribe","channel":"v4_trades",...}
[INFO] [MarketData] All dYdX subscriptions sent
```

If missing, check `send_subscription()` logic in `market_data_provider.cpp`.

---

## Summary

After running `./run.sh --release`, you will have:

1. ✅ **trading_engine_service** - With multi-exchange support
2. ✅ **test_market_data** - With multi-exchange support  
3. ✅ **multi_exchange_provider** - Example program

All three support **Bybit, Binance, and dYdX** out of the box!

**To test dYdX streaming**:
```bash
./build/release/multi_exchange_provider
```

Press **Ctrl+C** to stop gracefully.

🚀 **Your C++ multi-exchange provider is now ready!**
