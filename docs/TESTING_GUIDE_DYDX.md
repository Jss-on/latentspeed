# dYdX Streaming Test Guide

Comprehensive guide for testing dYdX WebSocket streaming integration.

---

## Quick Start

### Method 1: Automated Test Script (Recommended)

```bash
# Make executable
chmod +x test_dydx.sh

# Run all tests
./test_dydx.sh all

# Build project
./test_dydx.sh build

# Test WebSocket connection
./test_dydx.sh connection

# Start provider (Terminal 1)
./test_dydx.sh run

# Verify streaming (Terminal 2)
./test_dydx.sh verify

# Check orderbooks (Terminal 2)
./test_dydx.sh orderbook
```

### Method 2: Python Verification Tool

```bash
# Make executable
chmod +x examples/verify_dydx_streaming.py

# Health check
python3 examples/verify_dydx_streaming.py --health-check

# Monitor all streams
python3 examples/verify_dydx_streaming.py

# Monitor for 60 seconds
python3 examples/verify_dydx_streaming.py --duration 60

# Monitor trades only
python3 examples/verify_dydx_streaming.py --trades-only
```

### Method 3: Manual Testing

```bash
# Start provider
cd build/release
./test_market_data dydx BTC-USD,ETH-USD

# In another terminal, subscribe to ZMQ
python3 examples/test_zmq_subscriber.py --trades-port 5556
```

---

## Detailed Testing Procedures

## Test 1: Build Verification

Ensure all components compile successfully.

```bash
./test_dydx.sh build
```

**Expected Output:**
```
========================================
Test 1: Building Project
========================================
✅ Build completed successfully
-rwxr-xr-x 1 user user 2.1M multi_exchange_provider
-rwxr-xr-x 1 user user 1.8M test_market_data
```

**What it checks:**
- CMake configuration
- C++ compilation (exchange_interface.cpp, market_data_provider.cpp)
- Executable creation

---

## Test 2: WebSocket Connection Test

Verify direct connection to dYdX WebSocket API.

```bash
./test_dydx.sh connection
```

**Expected Output:**
```
========================================
Test 5: Testing dYdX WebSocket Connection
========================================
ℹ️  Testing connection to wss://indexer.dydx.trade/v4/ws
Connecting to dYdX...
✅ Connected successfully!
📤 Sent subscription for BTC-USD trades
📥 Received: subscribed
✅ Subscription confirmed
   Channel: v4_trades
   ID: BTC-USD
✅ dYdX connection test passed
```

**What it checks:**
- Network connectivity to dYdX
- TLS/SSL certificate validation
- WebSocket handshake
- Subscription message format
- Response parsing

**Troubleshooting:**
- **Timeout**: Check firewall/proxy settings
- **SSL Error**: Update system certificates
- **Connection refused**: Verify dYdX service status

---

## Test 3: Simple Provider Test

Run single-exchange provider with minimal configuration.

```bash
./test_dydx.sh simple
```

**Expected Output:**
```
[12:34:56.789] [info] Exchange: dydx
[12:34:56.789] [info] Symbols: [BTC-USD, ETH-USD]
[12:34:56.890] [info] Initializing market data provider...
[12:34:57.123] [info] Starting market data streaming...
[12:34:57.456] [TRADE] DYDX BTC-USD @ 65432.50 x 0.12340000 buy
[12:34:57.567] [ORDERBOOK] DYDX BTC-USD - Bid: 65432.00@1.50000000 | Ask: 65433.00@0.80000000
```

**What it checks:**
- Provider initialization
- Symbol normalization (USDT → USD)
- WebSocket connection
- Message parsing
- ZMQ publishing
- Trade and orderbook processing

---

## Test 4: Multi-Exchange Provider Test

Run provider using YAML configuration.

```bash
# This creates config_test_dydx.yml and starts provider
./test_dydx.sh run
```

**Config created (`config_test_dydx.yml`):**
```yaml
zmq:
  trades_port: 5556
  books_port: 5557
  window_size: 20
  depth_levels: 10

feeds:
  - exchange: dydx
    symbols:
      - BTC-USD
      - ETH-USD
      - SOL-USD
    enable_trades: true
    enable_orderbook: true
    snapshots_only: true
    snapshot_interval: 1
```

**Expected Output:**
```
============================================================
Feed handler running. Press Ctrl+C to stop.
ZMQ Ports: 5556 (trades), 5557 (orderbooks)
============================================================
--- Statistics Report ---
DYDX: 1234 msgs received, 1234 published, 0 errors
------------------------
```

---

## Test 5: Trade Stream Verification

Verify trades are being received and processed correctly.

**Terminal 1:**
```bash
./test_dydx.sh run
```

**Terminal 2:**
```bash
./test_dydx.sh verify
```

**Expected Output (Terminal 2):**
```
========================================
Test 3: Verifying dYdX ZMQ Messages
========================================
📊 Listening for dYdX trades...
--------------------------------------------------------------------------------

[1] 12:34:56.789 - DYDX-preprocessed_trades-BTC-USD
    Exchange: DYDX
    Symbol: BTC-USD
    Price: 65432.50
    Amount: 0.12340000
    Side: buy
    Transaction Price: 65432.50
    Volatility: 0.002345

[2] 12:34:57.123 - DYDX-preprocessed_trades-ETH-USD
    Exchange: DYDX
    Symbol: ETH-USD
    Price: 3456.78
    Amount: 0.45600000
    Side: sell
    Transaction Price: 3456.78
    Volatility: 0.001234

================================================================================
✅ Received 10 valid dYdX messages
   Exchanges: DYDX
   Symbols: BTC-USD, ETH-USD, SOL-USD
================================================================================
✅ dYdX ZMQ verification passed
```

**Message Format Validation:**
- ✅ Exchange field = "DYDX"
- ✅ Symbol format = "BTC-USD" (not "BTCUSDT")
- ✅ Price field present and valid
- ✅ Amount field present and valid
- ✅ Side field = "buy" or "sell"
- ✅ Preprocessed metrics included

---

## Test 6: Orderbook Stream Verification

Verify orderbook snapshots are correct.

```bash
./test_dydx.sh orderbook
```

**Expected Output:**
```
========================================
Test 4: Verifying dYdX OrderBook Data
========================================
📈 Listening for dYdX orderbook updates...
--------------------------------------------------------------------------------

[1] 12:34:58.123 - DYDX-preprocessed_book-BTC-USD
    Exchange: DYDX
    Symbol: BTC-USD
    Best Bid: 65432.00 @ 1.50000000
    Best Ask: 65433.00 @ 0.80000000
    Mid: 65432.50  Spread: 1.53 bps  Imbalance: 0.3043
    OFI: 0.0456  Vol: 0.002145

[2] 12:34:59.234 - DYDX-preprocessed_book-ETH-USD
    Exchange: DYDX
    Symbol: ETH-USD
    Best Bid: 3456.50 @ 2.30000000
    Best Ask: 3457.00 @ 1.20000000
    Mid: 3456.75  Spread: 1.45 bps  Imbalance: 0.3148
    OFI: -0.0234  Vol: 0.001567

================================================================================
✅ Received 5 valid orderbook updates
================================================================================
✅ dYdX orderbook verification passed
```

**Validates:**
- ✅ Bid/ask levels populated
- ✅ Prices in correct order (bids descending, asks ascending)
- ✅ Quantities > 0
- ✅ Midpoint calculation
- ✅ Spread calculation (basis points)
- ✅ Order flow imbalance (OFI)
- ✅ Level 1 imbalance
- ✅ Mid-price volatility

---

## Test 7: Advanced Python Monitor

Use the comprehensive Python monitoring tool.

```bash
python3 examples/verify_dydx_streaming.py
```

**Features:**
- Real-time message display
- Automatic statistics reporting (every 10 seconds)
- Price range tracking
- Message rate calculation
- Data quality checks

**Example Output:**
```
================================================================================
📡 dYdX STREAMING MONITOR
================================================================================
Press Ctrl+C to stop

[12:34:56.789] TRADE - BTC-USD
  Price: 65,432.50  Amount: 0.12340000  Side: buy
  Txn Price: 65,432.50
  Volatility: 0.002345
  Volume: 8,071.21

[12:34:57.123] ORDERBOOK - BTC-USD
  Bid: 65,432.00 @ 1.50000000
  Ask: 65,433.00 @ 0.80000000
  Mid: 65,432.50  Spread: 1.53 bps  Imbalance: 0.3043
  OFI: 0.0456  Vol: 0.002145

================================================================================
📊 STATISTICS (10.5s)
================================================================================

📈 TRADES: 42 total (4.00/sec)
  BTC-USD: 25 (2.38/sec)
  ETH-USD: 15 (1.43/sec)
  SOL-USD: 2 (0.19/sec)

📊 ORDERBOOKS: 105 total (10.00/sec)
  BTC-USD: 63 (6.00/sec)
  ETH-USD: 42 (4.00/sec)

💰 PRICE RANGES:
  BTC-USD: $65,100.00 - $65,600.00
  ETH-USD: $3,450.00 - $3,470.00
  SOL-USD: $125.50 - $126.20

================================================================================
```

---

## Health Check

Quick verification that streams are operational.

```bash
python3 examples/verify_dydx_streaming.py --health-check
```

**Expected Output:**
```
🏥 Running health check...
✅ Trades stream: HEALTHY
✅ Orderbook stream: HEALTHY
```

**Exit Codes:**
- `0`: All streams healthy
- `1`: One or more streams not receiving data

**Use in scripts:**
```bash
if python3 examples/verify_dydx_streaming.py --health-check; then
    echo "dYdX streaming is operational"
else
    echo "dYdX streaming issue detected"
    # Restart provider, alert, etc.
fi
```

---

## Performance Benchmarks

### Expected Throughput

| Symbol | Trades/sec | OrderBook Updates/sec |
|--------|-----------|----------------------|
| BTC-USD | 2-5 | 5-10 |
| ETH-USD | 1-3 | 5-10 |
| SOL-USD | 0.5-2 | 5-10 |
| **Total** | **5-15** | **15-30** |

### Latency Metrics

| Component | Expected Latency |
|-----------|-----------------|
| WebSocket RTT | 50-150ms |
| Message parsing | 1-3μs |
| ZMQ publish | 5-10μs |
| **End-to-end** | **60-170ms** |

### Resource Usage

| Metric | Expected |
|--------|----------|
| CPU | 5-10% per stream |
| Memory | ~10MB per feed |
| Network | 20-50 KB/s per symbol |

---

## Troubleshooting

### Issue: No messages received

**Symptoms:**
```
⏱️  Timeout waiting for messages
```

**Solutions:**

1. **Check provider is running:**
   ```bash
   pgrep -f "multi_exchange_provider\|test_market_data"
   ```

2. **Check logs:**
   ```bash
   tail -f /tmp/dydx_test.log
   ```

3. **Verify ZMQ ports:**
   ```bash
   netstat -tulpn | grep "5556\|5557"
   ```

4. **Test connection:**
   ```bash
   ./test_dydx.sh connection
   ```

---

### Issue: Invalid symbol format

**Symptoms:**
```
ERROR: Invalid market: BTC-USDT
```

**Solution:**
dYdX uses **USD** not **USDT**. Use `BTC-USD`, not `BTC-USDT`.

The provider auto-converts:
- `BTC-USDT` → `BTC-USD` ✅
- `BTCUSDT` → `BTC-USD` ✅
- `btc-usdt` → `BTC-USD` ✅

---

### Issue: SSL/TLS errors

**Symptoms:**
```
SSL certificate verify failed
```

**Solutions:**

1. **Update CA certificates:**
   ```bash
   sudo apt-get update && sudo apt-get install ca-certificates
   sudo update-ca-certificates
   ```

2. **Test with curl:**
   ```bash
   curl -v https://indexer.dydx.trade/v4/perpetualMarkets
   ```

---

### Issue: High latency

**Symptoms:**
- Delayed messages (> 500ms)
- Sparse updates

**Solutions:**

1. **Check network:**
   ```bash
   ping indexer.dydx.trade
   ```

2. **Check CPU usage:**
   ```bash
   top -p $(pgrep multi_exchange_provider)
   ```

3. **Enable debug logging:**
   Edit code and set: `spdlog::set_level(spdlog::level::debug);`

---

### Issue: Parse errors

**Symptoms:**
```
JSON parse error at position 42
```

**Solutions:**

1. **Check dYdX API version:**
   Ensure using v4: `wss://indexer.dydx.trade/v4/ws`

2. **Enable raw message logging:**
   ```cpp
   spdlog::debug("Raw message: {}", message);
   ```

3. **Verify message format:**
   Compare with [dYdX docs](https://docs.dydx.xyz/indexer-client/websockets)

---

## Integration Testing

### Test with Multiple Exchanges

```yaml
# config_multi.yml
feeds:
  - exchange: dydx
    symbols: ["BTC-USD", "ETH-USD"]
  - exchange: bybit
    symbols: ["BTC-USDT", "ETH-USDT"]
  - exchange: binance
    symbols: ["BTC-USDT", "ETH-USDT"]
```

```bash
./build/release/multi_exchange_provider --config config_multi.yml
```

**Verify all exchanges:**
```python
# Listen to all topics
sock.setsockopt_string(zmq.SUBSCRIBE, "")

# Filter by exchange
if data['exchange'] == 'DYDX':
    # dYdX-specific handling
```

---

## Continuous Integration

### Automated Test Script

```bash
#!/bin/bash
# ci_test_dydx.sh

set -e

echo "Building..."
./test_dydx.sh build

echo "Testing connection..."
if ! ./test_dydx.sh connection; then
    echo "Connection test failed"
    exit 1
fi

echo "Starting provider..."
./test_dydx.sh simple &
PROVIDER_PID=$!
sleep 5

echo "Running health check..."
if python3 examples/verify_dydx_streaming.py --health-check --duration 30; then
    echo "Health check passed"
else
    echo "Health check failed"
    kill $PROVIDER_PID
    exit 1
fi

echo "Stopping provider..."
kill $PROVIDER_PID
wait $PROVIDER_PID 2>/dev/null || true

echo "All tests passed!"
```

---

## Best Practices

### 1. Start Small
Begin with 1-2 symbols, then scale up:
```bash
./test_market_data dydx BTC-USD
```

### 2. Monitor Logs
Always check logs for warnings:
```bash
grep -i "error\|warning" /tmp/dydx_test.log
```

### 3. Use Health Checks
Integrate health checks in production:
```bash
*/5 * * * * /usr/local/bin/verify_dydx_streaming.py --health-check || /usr/local/bin/alert.sh
```

### 4. Compare with UI
Verify prices match [dYdX UI](https://trade.dydx.exchange/):
- Open BTC-USD market
- Compare last trade price
- Check bid/ask spread

### 5. Test Edge Cases
- Market open/close transitions
- Low liquidity symbols
- Network interruptions
- Rapid reconnection

---

## Summary

✅ **5 testing methods** provided  
✅ **Automated scripts** for quick testing  
✅ **Python tools** for advanced monitoring  
✅ **Health checks** for production  
✅ **Troubleshooting guide** for common issues  
✅ **Performance benchmarks** for validation  

**Quick test workflow:**
```bash
./test_dydx.sh build                               # Build
./test_dydx.sh connection                          # Test connection
./test_dydx.sh run &                               # Start provider
python3 examples/verify_dydx_streaming.py          # Monitor
```

dYdX streaming is now fully testable! 🚀
