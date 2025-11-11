# Reactive Trading System - Complete Setup

This guide shows how to set up a complete reactive trading system with:
- **Marketstream**: Real-time market data from Hyperliquid
- **Trading Engine**: Order execution on Hyperliquid testnet
- **Python Strategy**: Simple momentum-based reactive strategy

## Architecture

```
Hyperliquid Testnet (WebSocket)
         ↓
    Marketstream (C++)
         ↓ (ZMQ: ports 5556, 5557)
    Python Strategy
         ↓ (ZMQ: port 5601)
    Trading Engine (C++)
         ↓
Hyperliquid Testnet (REST API)
```

## Quick Start

### 1. Automated (Recommended)

```bash
cd /home/tensor/latentspeed
chmod +x examples/run_reactive_trading.sh
./examples/run_reactive_trading.sh
```

This starts all three components automatically.

### 2. Manual (Step by Step)

#### Step 1: Start Marketstream

```bash
# From latentspeed root
./build/release/marketstream configs/marketstream_hyperliquid.yml
```

This streams real-time market data:
- Trades on `tcp://127.0.0.1:5556`
- Orderbooks on `tcp://127.0.0.1:5557`

#### Step 2: Start Trading Engine

```bash
export HYPERLIQUID_USER_ADDRESS=0x44Fd91bEd5c87A4fFA222462798BB9d7Ef3669be
export HYPERLIQUID_PRIVATE_KEY=0x2e5aacf85446088b3121eec4eab06beda234decc4d16ffe3cb0d2a5ec25ea60b

./build/release/trading_engine_service \
    --exchange hyperliquid \
    --api-key $HYPERLIQUID_USER_ADDRESS \
    --api-secret $HYPERLIQUID_PRIVATE_KEY
```

This accepts orders on `tcp://127.0.0.1:5601` and publishes reports on `tcp://127.0.0.1:5602`.

#### Step 3: Start Python Strategy

```bash
python3 examples/strategy_simple_momentum.py \
    --symbol BTC \
    --size 0.001 \
    --max-position 0.01
```

## Strategy Details

### Simple Momentum Strategy

**Logic:**
1. Tracks recent trade prices (rolling window of 20 trades)
2. Calculates momentum: `(latest_price - oldest_price) / oldest_price`
3. If momentum > +0.05%: **BUY signal**
4. If momentum < -0.05%: **SELL signal**
5. Places limit orders slightly away from market price

**Parameters:**
- `--symbol`: Trading symbol (default: BTC)
- `--size`: Position size per order (default: 0.001)
- `--max-position`: Maximum total position (default: 0.01)
- `--window`: Momentum calculation window (default: 20 trades)
- `--threshold`: Momentum threshold for trading (default: 0.0005 = 0.05%)

**Risk Management:**
- Fixed position sizing
- Maximum position limits
- Order cooldown (5 seconds between orders)
- Limit orders only (no market orders)

## Expected Output

### Marketstream
```
[info] LatentSpeed MarketStream
[info] Starting 1 feed(s) with 5 total symbols...
[info] ZMQ Output:
[info]   - Trades:     tcp://127.0.0.1:5556
[info]   - Orderbooks: tcp://127.0.0.1:5557
```

### Trading Engine
```
[info] [HFT-Engine] Ultra-low latency trading engine initialized
[info] [HyperliquidAdapter] Testnet: true
[info] Connected to Hyperliquid WebSocket at api.hyperliquid-testnet.xyz
[info] Fetched trading rules for 198 pairs
```

### Strategy
```
[Strategy] Initialized for BTC
[Strategy] Position size: 0.001, Max: 0.01
[Strategy] Starting... Listening for BTC trades
[Signal] Positive momentum: 0.0012 - BUY signal
[Order] buy 0.001 BTC @ $50000.00 - Response: {'status': 'accepted'}
[Fill] Order momentum_1699999999000 filled
```

## Monitoring

### Real-time Logs

```bash
# Marketstream
tail -f logs/marketstream.log

# Trading Engine
tail -f logs/trading_engine.log

# Strategy (if running in background)
# Already printing to stdout
```

### Check Positions

The strategy prints stats every 30 seconds:
```
[Stats] Position: 0.0050, Last Price: $50123.45, Momentum: 0.0008, Orders: 15
```

## Customization

### Create Your Own Strategy

Use `strategy_simple_momentum.py` as a template:

```python
class MyStrategy:
    def __init__(self):
        # Subscribe to marketstream
        self.market_sub = zmq.Context().socket(zmq.SUB)
        self.market_sub.connect("tcp://127.0.0.1:5556")
        
        # Connect to trading engine
        self.order_client = zmq.Context().socket(zmq.REQ)
        self.order_client.connect("tcp://127.0.0.1:5601")
    
    def process_trade(self, trade_data):
        # Your logic here
        price = trade_data['price']
        
        # Send order
        order = {
            "action": "buy",
            "symbol": "BTC-USD",
            "quantity": 0.001,
            "price": price,
            "order_type": "limit"
        }
        self.order_client.send_json(order)
        response = self.order_client.recv_json()
```

### Strategy Ideas

1. **Market Making**: Place buy/sell orders around midpoint
2. **Mean Reversion**: Trade when price deviates from moving average
3. **Arbitrage**: Monitor multiple symbols for price divergence
4. **Liquidation Hunting**: Trade around liquidation levels
5. **Volume Profile**: Trade based on order book imbalances

## Troubleshooting

### Marketstream not connecting
```bash
# Check if Hyperliquid API is accessible
curl https://api.hyperliquid-testnet.xyz/info

# Check logs
tail -f logs/marketstream.log
```

### Strategy not receiving data
```bash
# Test ZMQ connection
python3 -c "import zmq; ctx = zmq.Context(); sock = ctx.socket(zmq.SUB); sock.connect('tcp://127.0.0.1:5556'); sock.setsockopt_string(zmq.SUBSCRIBE, ''); print(sock.recv_json()); sock.close(); ctx.term()"
```

### Orders not executing
```bash
# Check trading engine logs
tail -f logs/trading_engine.log | grep -i error

# Verify credentials are set
echo $HYPERLIQUID_USER_ADDRESS
```

### WebSocket disconnections
This is normal for idle connections. The system auto-reconnects:
```
[error] WebSocket error: End of file
[info] Reconnecting in 5 seconds...
[info] Connected to Hyperliquid WebSocket
```

## Safety Notes

⚠️ **TESTNET MODE** - Currently using Hyperliquid testnet with demo funds.

To switch to **MAINNET** (REAL MONEY):
```bash
# Add --live-trade flag
./build/release/trading_engine_service \
    --exchange hyperliquid \
    --api-key $HYPERLIQUID_USER_ADDRESS \
    --api-secret $HYPERLIQUID_PRIVATE_KEY \
    --live-trade  # ⚠️ REAL MONEY
```

**Before going live:**
1. Thoroughly test strategy on testnet
2. Implement proper risk management
3. Add position limits
4. Monitor constantly
5. Start with small position sizes

## Performance Tuning

### Low Latency Tips

1. **Pin CPU cores** for trading engine
2. **Increase priority** of critical processes
3. **Use localhost** for all ZMQ connections
4. **Reduce logging** in production
5. **Compile with** `-O3 -march=native -flto`

### Marketstream Optimization

```yaml
# configs/marketstream_hyperliquid.yml
zmq:
  window_size: 10  # Smaller window = lower latency
  
feeds:
  - exchange: hyperliquid
    symbols: [BTC]  # Only trade what you need
    snapshots_only: true  # Skip incremental updates
```

## Next Steps

1. **Backtest** your strategy on historical data
2. **Add multiple symbols** to diversify
3. **Implement stop-loss** logic
4. **Monitor slippage** and execution quality
5. **Scale up** position sizes gradually

## Support

For issues or questions:
- Check logs in `logs/` directory
- Review trading engine stats (printed every 10s)
- Monitor strategy output for errors
- Verify all ZMQ ports are free

Happy Trading! 🚀
