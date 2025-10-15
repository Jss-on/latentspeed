# Hyperliquid MarketStream Quick Start Guide

## Overview

This guide shows you how to stream real-time market data from Hyperliquid exchange using the latentspeed marketstream system.

## Architecture

```
Hyperliquid WebSocket → MarketStream → ZMQ → Your Trading Bot/Strategy
  (wss://api.hyperliquid.xyz/ws)    (Preprocessor)  (5556/5557)
```

## Step 1: Build the Project

```bash
# Build in release mode for production
./run.sh --release

# Or debug mode for development
./run.sh --debug
```

This creates two executables:
- `build/linux-release/marketstream` - Market data provider
- `build/linux-release/trading_engine_service` - Trading engine

## Step 2: Configure Hyperliquid Feed

The config file is already updated at `configs/config.yml` with Hyperliquid:

```yaml
feeds:
  - exchange: hyperliquid
    symbols:
      - BTC      # Bitcoin perpetual
      - ETH      # Ethereum perpetual
      - SOL      # Solana perpetual
      - AVAX     # Avalanche perpetual
      - ARB      # Arbitrum perpetual
    enable_trades: true
    enable_orderbook: true
    snapshots_only: true
    snapshot_interval: 1
```

### Customizing Symbols

Hyperliquid uses simple coin symbols. Add more from their supported markets:

```yaml
symbols:
  - BTC
  - ETH
  - SOL
  - AVAX
  - ARB
  - MATIC
  - DOGE
  - WIF
  - BONK
  # ... add more as needed
```

## Step 3: Run MarketStream

### Option A: From Build Directory (Recommended)

```bash
# Navigate to build directory
cd build/linux-release

# Run marketstream with config
./marketstream ../../configs/config.yml
```

### Option B: From Project Root

```bash
# Run with absolute path
./build/linux-release/marketstream configs/config.yml
```

## Expected Output

When running successfully, you'll see:

```
===========================================
LatentSpeed MarketStream
Production Market Data Provider (C++)
Config: ../../configs/config.yml
===========================================
[INFO] Adding hyperliquid feed: 5 symbols
[INFO] Adding dydx feed: 5 symbols
[INFO] Starting 2 feed(s) with 10 total symbols...
===========================================
Streaming market data (Press Ctrl+C to stop)
ZMQ Output:
  - Trades:     tcp://127.0.0.1:5556
  - Orderbooks: tcp://127.0.0.1:5557
===========================================

[DEBUG] [TRADE] HYPERLIQUID:BTC @ $50123.50 x 1.2500 buy
[DEBUG] [BOOK] HYPERLIQUID:ETH - Mid: $3045.23 Spread: 2.15 bps
...
```

## Step 4: Consume the Data

The market data is now available on ZMQ:

### A. Python Consumer Example

```python
import zmq

# Connect to trades stream
context = zmq.Context()
trades_socket = context.socket(zmq.SUB)
trades_socket.connect("tcp://127.0.0.1:5556")
trades_socket.setsockopt_string(zmq.SUBSCRIBE, "")

# Connect to orderbook stream
books_socket = context.socket(zmq.SUB)
books_socket.connect("tcp://127.0.0.1:5557")
books_socket.setsockopt_string(zmq.SUBSCRIBE, "")

# Receive data
while True:
    # Receive trade
    if trades_socket.poll(0):
        trade_data = trades_socket.recv_json()
        print(f"Trade: {trade_data}")
    
    # Receive orderbook
    if books_socket.poll(0):
        book_data = books_socket.recv_json()
        print(f"Book: {book_data}")
```

### B. With Trading Engine

Start the trading engine in another terminal:

```bash
cd build/linux-release

./trading_engine_service \
  --exchange bybit \
  --api-key YOUR_API_KEY \
  --api-secret YOUR_API_SECRET
```

The trading engine will automatically consume market data from ZMQ ports 5556/5557.

## Multi-Exchange Setup

You can run multiple exchanges simultaneously:

```yaml
feeds:
  # Hyperliquid
  - exchange: hyperliquid
    symbols: [BTC, ETH, SOL]
    enable_trades: true
    enable_orderbook: true
  
  # dYdX
  - exchange: dydx
    symbols: [BTC-USD, ETH-USD, SOL-USD]
    enable_trades: true
    enable_orderbook: true
  
  # Bybit
  - exchange: bybit
    symbols: [BTCUSDT, ETHUSDT, SOLUSDT]
    enable_trades: true
    enable_orderbook: true
```

All exchanges stream to the same ZMQ ports with exchange identification in the data.

## Performance Tuning

### 1. Adjust Log Level

For production, reduce logging overhead:

```yaml
log:
  filename: marketstream.log
  level: info  # or warn/error for less logging
```

### 2. Optimize ZMQ Buffer

For high-frequency data:

```yaml
zmq:
  enabled: true
  host: 127.0.0.1
  port: 5556
  window_size: 50      # Increase for more smoothing
  depth_levels: 20     # Increase for deeper orderbook
```

### 3. CPU Affinity

The marketstream automatically sets CPU affinity on Linux for optimal performance:
- CPU Core 6: WebSocket thread
- CPU Core 7: Processing thread

## Troubleshooting

### Connection Issues

**Problem:** Cannot connect to Hyperliquid
```
[ERROR] WebSocket connection failed
```

**Solution:** Check internet connection and firewall rules. Hyperliquid uses `wss://api.hyperliquid.xyz:443`

### No Data Received

**Problem:** MarketStream running but no trades/books
```
[INFO] Streaming market data...
(no debug output)
```

**Solution:** 
1. Set log level to `debug` in config
2. Check if symbols are correct (use simple names: BTC, ETH, not BTC-USD)
3. Verify `enable_trades` and `enable_orderbook` are true

### ZMQ Connection Refused

**Problem:** Consumer cannot connect to ZMQ
```
zmq.error.ZMQError: Connection refused
```

**Solution:**
1. Ensure marketstream is running first
2. Check if ports 5556/5557 are available: `netstat -an | grep 5556`
3. Firewall may be blocking: `sudo ufw allow 5556`

## Monitoring

### Check Process

```bash
# See if marketstream is running
ps aux | grep marketstream

# Check CPU/memory usage
top -p $(pgrep marketstream)
```

### View Logs

```bash
# Real-time log monitoring
tail -f marketstream.log

# Search for errors
grep ERROR marketstream.log

# Count messages
grep TRADE marketstream.log | wc -l
```

### Network Stats

```bash
# Check ZMQ connections
netstat -an | grep 5556
netstat -an | grep 5557

# Monitor bandwidth
sudo nethogs  # Install: sudo apt install nethogs
```

## Production Deployment

For production use:

1. **Use systemd service** (see `PRODUCTION.md`)
2. **Set log level to `info` or `warn`**
3. **Monitor with Prometheus/Grafana**
4. **Set up log rotation**
5. **Use release build** (`./run.sh --release`)

## Available Hyperliquid Symbols

Popular perpetuals on Hyperliquid (as of 2025):

```
BTC, ETH, SOL, AVAX, ARB, MATIC, DOGE, SHIB, 
WIF, BONK, JTO, PYTH, SEI, TIA, DYM, INJ, SUI, 
APT, OP, PEPE, ORDI, ATOM, DOT, LINK, UNI, 
AAVE, MKR, CRV, LDO, RPL, FXS, SNX
```

Check Hyperliquid's official docs for the complete updated list.

## Next Steps

- **Trading Integration**: See `docs/HYPERLIQUID_INTEGRATION.md`
- **Strategy Development**: Use `examples/sma_trading_strategy.py` as template
- **Multi-exchange Arbitrage**: Compare prices across Hyperliquid, dYdX, Binance
- **Low-latency Optimization**: See `docs/HFT_OPTIMIZATIONS.md`

## Support

- Documentation: `docs/HYPERLIQUID_INTEGRATION.md`
- Example Code: `examples/hyperliquid_example.cpp`
- Configuration: `examples/config_hyperliquid.yml`

## License

See main project LICENSE file.
