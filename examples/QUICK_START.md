# Quick Start - Reactive Trading System

## What Was Fixed

The strategy was failing because **marketstream sends multipart ZMQ messages**:
1. **Topic** (e.g., "HYPERLIQUID-preprocessed_trades-BTC")
2. **JSON data** (the actual trade/orderbook data)

The Python strategy was trying to receive a single JSON message, causing the decode error.

**Fix applied**: Changed from `recv_json()` to `recv_string()` + `recv_json()` to properly handle multipart messages.

## Step-by-Step Setup

### 1. Setup Python Environment (One-time)

```bash
cd /home/tensor/latentspeed
chmod +x examples/setup_strategy_env.sh
./examples/setup_strategy_env.sh
```

This creates `.venv/` with required dependencies (pyzmq).

### 2. Test Marketstream Connection (Recommended)

First, make sure marketstream is running and sending data:

```bash
# Terminal 1: Start marketstream
./build/release/marketstream configs/marketstream_hyperliquid.yml

# Terminal 2: Test receiving data
source .venv/bin/activate
python3 examples/test_marketstream.py
```

You should see:
```
[TRADE #1] Topic: HYPERLIQUID-preprocessed_trades-BTC
  Exchange: HYPERLIQUID
  Symbol: BTC
  Price: $50123.45
  Amount: 0.0123
  Side: buy
```

If you see trades/books coming through, marketstream is working! Press Ctrl+C and proceed.

### 3. Start Full Trading System

```bash
chmod +x examples/run_reactive_trading.sh
./examples/run_reactive_trading.sh
```

This starts:
1. ✅ **Marketstream** - Streams market data from Hyperliquid
2. ✅ **Trading Engine** - Connects to Hyperliquid for order execution  
3. ✅ **Strategy** - Python momentum strategy

## Expected Output

### Marketstream (logs/marketstream.log)
```
[info] LatentSpeed MarketStream
[info] Adding HYPERLIQUID feed: 5 symbols
[info] Starting 1 feed(s) with 5 total symbols...
[info] ZMQ Output:
[info]   - Trades:     tcp://127.0.0.1:5556
[info]   - Orderbooks: tcp://127.0.0.1:5557
```

### Trading Engine (logs/trading_engine.log)
```
[info] [HFT-Engine] Ultra-low latency trading engine initialized
[info] [HyperliquidAdapter] Testnet: true
[info] Connected to Hyperliquid WebSocket at api.hyperliquid-testnet.xyz
[info] Fetched trading rules for 198 pairs
```

### Strategy (stdout)
```
[Strategy] Initialized for BTC
[Strategy] Position size: 0.001, Max: 0.01
[Strategy] Starting... Listening for BTC trades
[Signal] Positive momentum: 0.0012 - BUY signal
[Order] buy 0.001 BTC @ $50000.00 - Response: {'status': 'accepted'}
```

## Troubleshooting

### "No data received" in test_marketstream.py

**Problem**: Marketstream might not be running or not connected to Hyperliquid.

**Solution**:
```bash
# Check if marketstream is running
ps aux | grep marketstream

# Check marketstream logs
tail -f logs/marketstream.log | grep -i "connected\|error"
```

### "Connection refused" errors

**Problem**: Ports 5556/5557 (marketstream) or 5601/5602 (trading engine) are not available.

**Solution**:
```bash
# Check if ports are in use
netstat -tlnp | grep -E "5556|5557|5601|5602"

# Kill any processes using these ports
sudo lsof -ti:5556 | xargs kill -9
```

### Strategy receives messages but doesn't trade

**Problem**: Either no momentum signals or position limits reached.

**Check**:
- Look for `[Signal]` messages in strategy output
- Verify `current_position` in stats (printed every 30s)
- Lower the `--threshold` to trade more frequently:
  ```bash
  python3 examples/strategy_simple_momentum.py --threshold 0.0001  # 0.01% instead of 0.05%
  ```

### Orders rejected by trading engine

**Problem**: Invalid credentials or API limits.

**Check**:
```bash
# Verify credentials are set
echo $HYPERLIQUID_USER_ADDRESS
echo $HYPERLIQUID_PRIVATE_KEY

# Check trading engine logs for errors
tail -f logs/trading_engine.log | grep -i "reject\|error"
```

## Customization

### Trade Different Symbol

```bash
python3 examples/strategy_simple_momentum.py --symbol ETH
```

### Adjust Position Size

```bash
python3 examples/strategy_simple_momentum.py \
    --size 0.01 \          # Larger position
    --max-position 0.1     # Higher limit
```

### More Sensitive (Trade More Often)

```bash
python3 examples/strategy_simple_momentum.py \
    --window 10 \          # Shorter window
    --threshold 0.0002     # Lower threshold (0.02%)
```

### Less Sensitive (Trade Less Often)

```bash
python3 examples/strategy_simple_momentum.py \
    --window 50 \          # Longer window
    --threshold 0.001      # Higher threshold (0.1%)
```

## Architecture Diagram

```
Hyperliquid Testnet WebSocket
         ↓
   ┌─────────────┐
   │ Marketstream│  (C++ - Ultra fast)
   │ (Port 5556) │  Trades
   │ (Port 5557) │  Orderbooks  
   └─────────────┘
         ↓ ZMQ Multipart: [topic, json_data]
   ┌─────────────┐
   │   Strategy  │  (Python - Flexible)
   │             │  - Momentum calculation
   │             │  - Signal generation
   └─────────────┘
         ↓ ZMQ REQ/REP: order commands
   ┌─────────────┐
   │Trading Engine│ (C++ - Ultra low latency)
   │ (Port 5601) │  Orders IN
   │ (Port 5602) │  Reports OUT
   └─────────────┘
         ↓ REST API + WebSocket
   Hyperliquid Testnet
```

## ZMQ Message Formats

### Trade Message (Port 5556)
```
Part 1 (Topic):   "HYPERLIQUID-preprocessed_trades-BTC"
Part 2 (JSON):    {
                    "exchange": "HYPERLIQUID",
                    "symbol": "BTC",
                    "price": 50123.45,
                    "amount": 0.0123,
                    "side": "buy",
                    "timestamp": 1699999999000
                  }
```

### Order Command (Port 5601 - REQ/REP)
```python
# Request
{
    "action": "buy",
    "symbol": "BTC-USD",
    "quantity": 0.001,
    "price": 50000.0,
    "order_type": "limit",
    "client_order_id": "momentum_1699999999000"
}

# Response
{
    "status": "accepted",
    "client_order_id": "momentum_1699999999000",
    "exchange_order_id": "123456"
}
```

## Stopping the System

Press `Ctrl+C` in the terminal running `run_reactive_trading.sh`. This will gracefully shutdown all three components.

Or manually:
```bash
# Find PIDs
ps aux | grep -E "marketstream|trading_engine|strategy_simple"

# Kill them
killall marketstream trading_engine_service python3
```

## Next Steps

1. ✅ **Verify data flow** with test_marketstream.py
2. ✅ **Run the system** with run_reactive_trading.sh
3. 📊 **Monitor** the strategy output for signals and orders
4. 🔧 **Customize** parameters for your risk tolerance
5. 📈 **Backtest** on historical data before going live
6. 🚀 **Scale up** gradually as you gain confidence

## Support

- **Logs**: Check `logs/` directory
- **Stats**: Strategy prints stats every 30s
- **Debug**: Run test_marketstream.py to isolate issues
- **Help**: See REACTIVE_TRADING_README.md for full docs

Happy Trading! 🎯
