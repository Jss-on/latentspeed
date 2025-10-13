# Bybit Market Data Testing Guide

**Created**: 2025-10-08  
**Purpose**: Comprehensive testing guide for Bybit integration with multi-exchange provider  
**Status**: Ready for testing after build completes

---

## Prerequisites

### 1. Install yaml-cpp Dependency

```bash
cd /home/tensor/latentspeed

# vcpkg will auto-install when you rebuild
# yaml-cpp was added to vcpkg.json
```

### 2. Clean and Rebuild

```bash
cd /home/tensor/latentspeed

# Clean previous build
rm -rf build/release

# Rebuild with new dependencies
./run.sh --release
```

**Expected output**:
```
[INFO] Installing yaml-cpp...
[INFO] Building CXX objects...
[INFO] Linking executables...
[100%] Built target multi_exchange_provider
```

---

## Test 1: Verify Build Artifacts

```bash
cd /home/tensor/latentspeed/build/release

# Check if all executables exist
ls -lh trading_engine_service
ls -lh test_market_data
ls -lh multi_exchange_provider

# Expected: All three executables should exist and be ~2-5MB each
```

**Expected output**:
```
-rwxr-xr-x 1 user user 3.2M Oct  8 22:00 trading_engine_service
-rwxr-xr-x 1 user user 2.8M Oct  8 22:00 test_market_data
-rwxr-xr-x 1 user user 2.9M Oct  8 22:00 multi_exchange_provider
```

---

## Test 2: Multi-Exchange Provider - Bybit Only

### Create Test Config (Bybit Only)

```bash
cat > /home/tensor/latentspeed/config_bybit_only.yml << 'EOF'
zmq:
  trades_port: 5556
  books_port: 5557
  window_size: 20
  depth_levels: 10

feeds:
  - exchange: bybit
    symbols:
      - BTC-USDT
      - ETH-USDT
    enable_trades: true
    enable_orderbook: true
    snapshots_only: true
    snapshot_interval: 1
EOF
```

### Run Multi-Exchange Provider

```bash
cd /home/tensor/latentspeed/build/release

# Run with Bybit-only config
./multi_exchange_provider --config ../../config_bybit_only.yml
```

**Expected output**:
```
[INFO] ============================================================
[INFO] Multi-Exchange Market Data Provider
[INFO] Similar to Python cryptofeed FeedHandler
[INFO] ============================================================
[INFO] Loading configuration from: ../../config_bybit_only.yml
[INFO] Added 1 feeds
[INFO] Starting feed handler...
[INFO] [FeedHandler] Starting feed: bybit
[INFO] [MarketData] Initializing provider for exchange: bybit
[INFO] [MarketData] Exchange interface: BYBIT
[INFO] [MarketData] Connecting to WebSocket: wss://stream.bybit.com:443/v5/public/spot
[INFO] [MarketData] WebSocket connected successfully
[INFO] [MarketData] Subscription message: {"op":"subscribe","args":["publicTrade.BTCUSDT","publicTrade.ETHUSDT","orderbook.10.BTCUSDT","orderbook.10.ETHUSDT"]}
[INFO] [MarketData] Subscription sent successfully (123 bytes)
[INFO] ============================================================
[INFO] Feed handler running. Press Ctrl+C to stop.
[INFO] ZMQ Ports: 5556 (trades), 5557 (orderbooks)
[INFO] ============================================================
[TRADE] BYBIT BTC-USDT @ 65432.50 x 0.00150000 buy
[TRADE] BYBIT ETH-USDT @ 3245.67 x 0.25000000 sell
[BOOK] BYBIT BTC-USDT - Mid: 65432.75 Spread: 0.0015% Vol: 0.0023 OFI: 0.1234
```

### Verify Connectivity

✅ **WebSocket connected successfully**  
✅ **Subscription sent**  
✅ **Trade messages appearing**  
✅ **Orderbook messages appearing**

---

## Test 3: ZMQ Message Verification

Open a **new terminal** while `multi_exchange_provider` is running:

### Test 3a: Subscribe to All Bybit Trades

```bash
python3 << 'EOF'
import zmq
import json
import time

ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect("tcp://127.0.0.1:5556")
sock.setsockopt_string(zmq.SUBSCRIBE, "BYBIT-")

print("=== Listening to Bybit Trades (Port 5556) ===")
print("Press Ctrl+C to stop\n")

count = 0
start = time.time()

try:
    while True:
        topic = sock.recv_string()
        message = sock.recv_string()
        
        count += 1
        
        # Parse and display
        data = json.loads(message)
        print(f"[{count}] {topic}")
        print(f"    Price: {data['price']}")
        print(f"    Amount: {data['amount']}")
        print(f"    Side: {data['side']}")
        print(f"    Time: {data['preprocessing_timestamp']}")
        print()
        
        # Show rate every 10 messages
        if count % 10 == 0:
            elapsed = time.time() - start
            rate = count / elapsed
            print(f">>> Rate: {rate:.2f} messages/sec\n")
            
except KeyboardInterrupt:
    elapsed = time.time() - start
    print(f"\n=== Statistics ===")
    print(f"Total messages: {count}")
    print(f"Elapsed time: {elapsed:.2f}s")
    print(f"Average rate: {count/elapsed:.2f} msg/sec")
EOF
```

**Expected output**:
```
=== Listening to Bybit Trades (Port 5556) ===
Press Ctrl+C to stop

[1] BYBIT-preprocessed_trades-BTC-USDT
    Price: 65432.5
    Amount: 0.0015
    Side: buy
    Time: 2025-10-08T22:15:30.123Z

[2] BYBIT-preprocessed_trades-ETH-USDT
    Price: 3245.67
    Amount: 0.25
    Side: sell
    Time: 2025-10-08T22:15:30.456Z

>>> Rate: 45.32 messages/sec
```

### Test 3b: Subscribe to Bybit Orderbooks

```bash
python3 << 'EOF'
import zmq
import json

ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect("tcp://127.0.0.1:5557")
sock.setsockopt_string(zmq.SUBSCRIBE, "BYBIT-preprocessed_book-")

print("=== Listening to Bybit Orderbooks (Port 5557) ===\n")

count = 0
try:
    while count < 10:  # Show first 10 orderbooks
        topic = sock.recv_string()
        message = sock.recv_string()
        
        count += 1
        data = json.loads(message)
        
        print(f"[{count}] {data['exchange']} {data['symbol']}")
        print(f"    Midpoint: {data['midpoint']:.2f}")
        print(f"    Spread: {data['relative_spread']*10000:.2f} bps")
        print(f"    Imbalance: {data['imbalance_lvl1']:.4f}")
        print(f"    Best Bid: {data['bids'][0]['price']:.2f} x {data['bids'][0]['quantity']:.4f}")
        print(f"    Best Ask: {data['asks'][0]['price']:.2f} x {data['asks'][0]['quantity']:.4f}")
        print()
        
except KeyboardInterrupt:
    pass
    
print(f"\nReceived {count} orderbook snapshots")
EOF
```

**Expected output**:
```
=== Listening to Bybit Orderbooks (Port 5557) ===

[1] BYBIT BTC-USDT
    Midpoint: 65432.75
    Spread: 1.53 bps
    Imbalance: 0.1234
    Best Bid: 65432.50 x 1.2500
    Best Ask: 65433.00 x 0.8500

[2] BYBIT ETH-USDT
    Midpoint: 3245.50
    Spread: 1.85 bps
    Imbalance: -0.0567
    Best Bid: 3245.45 x 5.5000
    Best Ask: 3245.55 x 4.2000
```

---

## Test 4: Performance Metrics

### Test 4a: Message Latency

```bash
python3 << 'EOF'
import zmq
import json
import time

ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect("tcp://127.0.0.1:5556")
sock.setsockopt_string(zmq.SUBSCRIBE, "BYBIT-")

print("=== Measuring End-to-End Latency ===\n")

latencies = []

for i in range(100):
    topic = sock.recv_string()
    message = sock.recv_string()
    
    recv_time = time.time_ns()
    data = json.loads(message)
    
    # Calculate latency (current time - receipt timestamp)
    receipt_ns = data['receipt_timestamp_ns']
    latency_us = (recv_time - receipt_ns) / 1000
    latencies.append(latency_us)
    
    if i % 10 == 0:
        print(f"Message {i+1}: {latency_us:.2f} μs")

# Statistics
latencies.sort()
avg = sum(latencies) / len(latencies)
p50 = latencies[50]
p95 = latencies[95]
p99 = latencies[99]

print(f"\n=== Latency Statistics (100 messages) ===")
print(f"Average: {avg:.2f} μs")
print(f"Median (p50): {p50:.2f} μs")
print(f"p95: {p95:.2f} μs")
print(f"p99: {p99:.2f} μs")
EOF
```

**Expected output**:
```
=== Measuring End-to-End Latency ===

Message 1: 2345.67 μs
Message 11: 2123.45 μs
Message 21: 2567.89 μs
...

=== Latency Statistics (100 messages) ===
Average: 2345.67 μs
Median (p50): 2234.56 μs
p95: 3456.78 μs
p99: 4567.89 μs
```

### Test 4b: Throughput Test

```bash
python3 << 'EOF'
import zmq
import time

ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect("tcp://127.0.0.1:5556")
sock.setsockopt_string(zmq.SUBSCRIBE, "")  # All messages

print("=== Measuring Throughput ===")
print("Collecting for 30 seconds...\n")

start = time.time()
count = 0

while time.time() - start < 30:
    topic = sock.recv_string()
    message = sock.recv_string()
    count += 1

elapsed = time.time() - start
rate = count / elapsed

print(f"=== Results ===")
print(f"Total messages: {count}")
print(f"Elapsed time: {elapsed:.2f}s")
print(f"Throughput: {rate:.2f} msg/sec")
print(f"Throughput: {rate*60:.2f} msg/min")
EOF
```

**Expected output**:
```
=== Measuring Throughput ===
Collecting for 30 seconds...

=== Results ===
Total messages: 1523
Elapsed time: 30.00s
Throughput: 50.77 msg/sec
Throughput: 3046.00 msg/min
```

---

## Test 5: Statistics Monitoring

### View Live Statistics

The `multi_exchange_provider` prints statistics every 10 seconds:

```
[INFO] --- Statistics Report ---
[INFO] bybit: 1523 msgs received, 1523 published, 0 errors
[INFO] ------------------------
```

### Query Statistics Programmatically

Add this to your test:

```cpp
// In your code
auto stats = g_feed_handler->get_stats();
for (const auto& feed_stats : stats) {
    std::cout << "Exchange: " << feed_stats.exchange << std::endl;
    std::cout << "  Messages Received: " << feed_stats.messages_received << std::endl;
    std::cout << "  Messages Published: " << feed_stats.messages_published << std::endl;
    std::cout << "  Errors: " << feed_stats.errors << std::endl;
}
```

---

## Test 6: Multi-Symbol Load Test

### Config with Many Symbols

```yaml
# config_bybit_stress.yml
feeds:
  - exchange: bybit
    symbols:
      - BTC-USDT
      - ETH-USDT
      - SOL-USDT
      - AVAX-USDT
      - MATIC-USDT
      - DOGE-USDT
      - XRP-USDT
      - ADA-USDT
      - DOT-USDT
      - LINK-USDT
    enable_trades: true
    enable_orderbook: true
    snapshots_only: true
```

```bash
./multi_exchange_provider --config ../../config_bybit_stress.yml
```

**Monitor**:
- CPU usage: Should stay under 20% per feed
- Memory usage: ~5-10MB per symbol
- Message rate: 200-500 msg/sec total

---

## Test 7: Error Handling

### Test 7a: Invalid Symbol

```yaml
feeds:
  - exchange: bybit
    symbols:
      - INVALID-SYMBOL  # Should fail gracefully
      - BTC-USDT        # Should work
```

**Expected**: Error logged, but doesn't crash

### Test 7b: Network Interruption

While running, disable network briefly:

```bash
# Simulate network issue
sudo iptables -A OUTPUT -d stream.bybit.com -j DROP
sleep 5
sudo iptables -D OUTPUT -d stream.bybit.com -j DROP
```

**Expected**: Reconnect automatically after ~10-30 seconds

### Test 7c: Malformed Config

```yaml
feeds:
  - exchange: nonexistent_exchange  # Invalid exchange
    symbols: []
```

**Expected**: Clear error message, program exits gracefully

---

## Test 8: Graceful Shutdown

### Test SIGINT (Ctrl+C)

```bash
./multi_exchange_provider

# Press Ctrl+C
```

**Expected output**:
```
^C[INFO] Received signal 2, shutting down...
[INFO] [FeedHandler] Stopping all feeds...
[INFO] [FeedHandler] Stopping feed: bybit
[INFO] [MarketData] Stopping market data provider
[INFO] [MarketData] WebSocket disconnected
[INFO] [FeedHandler] All feeds stopped
[INFO] Shutdown complete
```

### Test SIGTERM

```bash
# In another terminal
kill -TERM $(pgrep multi_exchange_provider)
```

**Expected**: Same graceful shutdown

---

## Test 9: Data Quality Checks

### Validate Trade Data

```python
import zmq
import json

ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect("tcp://127.0.0.1:5556")
sock.setsockopt_string(zmq.SUBSCRIBE, "BYBIT-")

print("=== Validating Trade Data Quality ===\n")

issues = []

for i in range(100):
    topic = sock.recv_string()
    message = sock.recv_string()
    data = json.loads(message)
    
    # Check required fields
    required = ['exchange', 'symbol', 'price', 'amount', 'side', 'timestamp_ns']
    for field in required:
        if field not in data:
            issues.append(f"Message {i}: Missing field '{field}'")
    
    # Validate data types
    if data.get('price', 0) <= 0:
        issues.append(f"Message {i}: Invalid price {data['price']}")
    
    if data.get('amount', 0) <= 0:
        issues.append(f"Message {i}: Invalid amount {data['amount']}")
    
    if data.get('side') not in ['buy', 'sell']:
        issues.append(f"Message {i}: Invalid side '{data['side']}'")

if issues:
    print("❌ FAILED: Found issues:")
    for issue in issues:
        print(f"  - {issue}")
else:
    print("✅ PASSED: All 100 messages valid")
```

---

## Test 10: Comparison with Python Cryptofeed

### Run Python Cryptofeed (Reference)

```python
from cryptofeed import FeedHandler
from cryptofeed.exchanges import Bybit
from cryptofeed.defines import TRADES
import time

start = time.time()
count = [0]

def callback(trade, receipt_timestamp):
    count[0] += 1

fh = FeedHandler()
fh.add_feed(Bybit(symbols=['BTC-USDT'], channels=[TRADES], callbacks={TRADES: callback}))

fh.run()

# After 30 seconds
elapsed = time.time() - start
print(f"Python rate: {count[0]/elapsed:.2f} msg/sec")
```

### Run C++ Implementation

```bash
./multi_exchange_provider  # Run for 30 seconds
```

**Compare**:
- **Python**: ~50-100 msg/sec, ~500MB memory
- **C++**: ~50-100 msg/sec (same), **~100MB memory** (5x less!)
- **Latency**: C++ should be 10-100x faster

---

## Success Criteria

### ✅ Build
- [ ] All executables compile without errors
- [ ] yaml-cpp dependency installed
- [ ] No missing symbols at link time

### ✅ Connectivity
- [ ] WebSocket connects to Bybit
- [ ] Subscription message sent
- [ ] Receives response from exchange

### ✅ Data Flow
- [ ] Trade messages appear in logs
- [ ] Orderbook messages appear in logs
- [ ] ZMQ publishes to ports 5556/5557

### ✅ ZMQ Integration
- [ ] Can subscribe to trades on port 5556
- [ ] Can subscribe to orderbooks on port 5557
- [ ] Messages are valid JSON
- [ ] All required fields present

### ✅ Performance
- [ ] Latency < 5ms (excluding network)
- [ ] Throughput > 40 msg/sec per symbol
- [ ] Memory < 10MB per symbol
- [ ] CPU < 20% per feed

### ✅ Reliability
- [ ] Runs for > 1 hour without crashes
- [ ] Handles network issues gracefully
- [ ] Graceful shutdown on signals
- [ ] No memory leaks (valgrind clean)

### ✅ Data Quality
- [ ] All messages have required fields
- [ ] Price/amount values are positive
- [ ] Timestamps are reasonable
- [ ] Side is 'buy' or 'sell'

---

## Troubleshooting

### Issue: Build fails with yaml-cpp errors

**Solution**:
```bash
cd build/release
cmake ../.. -DCMAKE_BUILD_TYPE=Release
ninja clean
ninja
```

### Issue: No messages received

**Check**:
1. WebSocket connected? (check logs)
2. Subscription sent? (check logs)
3. Firewall blocking? (`telnet stream.bybit.com 443`)
4. Symbol format correct? (should be `BTC-USDT`)

### Issue: ZMQ subscriber gets no messages

**Check**:
1. Correct port? (5556 for trades, 5557 for books)
2. Correct topic? (`BYBIT-preprocessed_trades-BTC-USDT`)
3. Provider still running? (`ps aux | grep multi_exchange`)

### Issue: High CPU usage

**Check**:
1. Too many symbols? (reduce to 3-5 for testing)
2. Logging level too verbose? (set to `info` not `debug`)

---

## Next Steps

After Bybit tests pass:

1. **Test Binance**: Change config to use `binance` exchange
2. **Test dYdX**: Change config to use `dydx` exchange
3. **Test Multi-Exchange**: Run Bybit + Binance + dYdX simultaneously
4. **Load Testing**: 20+ symbols across 3 exchanges
5. **Endurance Testing**: 24-hour stability test

---

## Summary

This guide covers **10 comprehensive tests** for Bybit integration:

1. ✅ Build verification
2. ✅ Basic connectivity
3. ✅ ZMQ message flow
4. ✅ Performance metrics
5. ✅ Statistics monitoring
6. ✅ Multi-symbol stress test
7. ✅ Error handling
8. ✅ Graceful shutdown
9. ✅ Data quality validation
10. ✅ Comparison with Python

**Estimated testing time**: 1-2 hours

**All tests should pass** before moving to production! 🚀
