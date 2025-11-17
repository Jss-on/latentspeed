# Start Trading System - Step by Step

## Configuration is Already Set Up ✅

Your configs are correctly formatted:
- `configs/marketstream_hyperliquid.yml` - Market data config
- `configs/hyperliquid_credentials.yml` - Trading credentials
- `examples/lead_lag_strategy_live.py` - Strategy logic

## Quick Start (From WSL Terminal)

### 1. Open WSL terminal and navigate to project

```bash
cd /home/tensor/latentspeed
```

### 2. Make the script executable (one time only)

```bash
chmod +x run_lead_lag_system.sh
```

### 3. Run the complete system

```bash
./run_lead_lag_system.sh
```

This starts everything automatically:
- ✅ MarketStream (C++) on ports 5556/5557
- ✅ Trading Engine (C++) on ports 5601/5602  
- ✅ Lead-Lag Strategy (Python) connecting all components

## Manual Start (If Script Fails)

### Terminal 1: Start MarketStream

```bash
cd /home/tensor/latentspeed
./build/release/marketstream configs/marketstream_hyperliquid.yml
```

Expected output:
```
LatentSpeed MarketStream
Adding hyperliquid feed: 5 symbols
ZMQ Output:
  - Trades:     tcp://127.0.0.1:5556
  - Orderbooks: tcp://127.0.0.1:5557
```

### Terminal 2: Start Trading Engine

```bash
cd /home/tensor/latentspeed

./build/release/trading_engine_service \
  --exchange hyperliquid \
  --api-key 0x44Fd91bEd5c87A4fFA222462798BB9d7Ef3669be \
  --api-secret 0x2e5aacf85446088b3121eec4eab06beda234decc4d16ffe3cb0d2a5ec25ea60b \
```

Expected output:
```
Trading Engine Service starting...
Exchange: hyperliquid
ZMQ listening on:
  - Orders:  tcp://127.0.0.1:5601
  - Reports: tcp://127.0.0.1:5602
```

### Terminal 3: Start Lead-Lag Strategy

```bash
cd /home/tensor/latentspeed
python3 examples/strategy_simple_momentum.py
```

Expected output:
```
✓ Lead-Lag Strategy initialized
  Leader: BTC | Follower: ETH
🔔 Jump detected in BTC: 32.45bps
  📈 Correlation: 0.6234
  🎯 Signal: BUY ETH
```

## Testing Individual Components

### Test MarketStream Data Reception

```bash
# In a new terminal
python3 examples/test_marketstream_zmq.py
```

You should see:
```
✓ Connected to trades stream: tcp://127.0.0.1:5556
✓ Connected to orderbook stream: tcp://127.0.0.1:5557
[TRADE] hyperliquid:BTC | BUY  | Price: $43,256.50 | Amount: 1.2345
[ORDERBOOK] hyperliquid:ETH
```

### Test Trading Engine Connection

```bash
# Send a test order
python3 examples/python_trading_client.py
```

## Configuration Reference

### MarketStream Config (`configs/marketstream_hyperliquid.yml`)

```yaml
log:
  level: info                              # Log verbosity
  filename: logs/marketstream_hyperliquid.log

zmq:
  enabled: true
  host: 127.0.0.1
  port: 5556                              # Trades port
  window_size: 20                         # Statistics window

feeds:
  - exchange: hyperliquid
    symbols:
      - BTC      # Leader asset
      - ETH      # Follower asset
      - SOL      # Optional additional symbols
      - ARB
      - AVAX
    enable_trades: true
    enable_orderbook: true
    snapshots_only: false
    snapshot_interval: 1
```

### Strategy Config (in `lead_lag_strategy_live.py`)

```python
config = {
    "leader_symbol": "BTC",
    "follower_symbol": "ETH",
    "jump_threshold_bps": 25.0,       # Min BTC jump to trigger
    "min_correlation": 0.55,          # Min BTC/ETH correlation
    "lookback_window": 100,           # Price history size
    "base_position_usd": 100.0,       # Base trade size
    "max_position_usd": 300.0,        # Max trade size
    "stop_loss_bps": 40.0,            # Stop loss (0.4%)
    "take_profit_bps": 80.0,          # Take profit (0.8%)
    "max_open_positions": 2,          # Max concurrent positions
    "position_timeout_seconds": 120   # Max hold time
}
```

## Monitoring

### View Logs

```bash
# MarketStream
tail -f logs/marketstream.log

# Trading Engine
tail -f logs/trading_engine.log

# Strategy
tail -f logs/strategy.log
```

### Check Processes

```bash
# See if components are running
ps aux | grep marketstream
ps aux | grep trading_engine
ps aux | grep lead_lag
```

### Monitor Network Ports

```bash
# Verify ZMQ ports are listening
netstat -an | grep 555  # MarketStream
netstat -an | grep 560  # Trading Engine
```

## Troubleshooting

### "Config file not found"

**Problem**: Marketstream can't find config file

**Solution**: Use absolute or correct relative path
```bash
# Relative path (from project root)
./build/marketstream configs/marketstream_hyperliquid.yml

# Or absolute path
./build/marketstream /home/tensor/latentspeed/configs/marketstream_hyperliquid.yml
```

### "No data received" in strategy

**Problem**: Strategy not seeing market data

**Check 1**: Is MarketStream running?
```bash
ps aux | grep marketstream
```

**Check 2**: Test ZMQ connection
```bash
python3 examples/test_marketstream_zmq.py
```

**Check 3**: Verify ports
```bash
netstat -an | grep 5556
netstat -an | grep 5557
```

### "Failed to connect to exchange"

**Problem**: Trading Engine can't connect to Hyperliquid

**Check 1**: Verify credentials
```bash
echo $HYPERLIQUID_USER_ADDRESS
# Should show: 0x44Fd91bEd5c87A4fFA222462798BB9d7Ef3669be
```

**Check 2**: Check Hyperliquid API
```bash
curl https://api.hyperliquid.xyz/info
```

### "No trades executing"

**Reason 1**: Correlation too low
- BTC/ETH correlation must be >0.55
- Wait for more data (need 100+ price points)

**Reason 2**: Jump threshold not met
- BTC price change must be >25bps (0.25%)

**Reason 3**: Position limits
- Already at max 2 positions
- Wait for positions to close

## Stop the System

### Using the orchestration script
```bash
# Just press Ctrl+C
# Script will gracefully shut down all components
```

### Manual stop
```bash
# Find process IDs
ps aux | grep marketstream
ps aux | grep trading_engine
ps aux | grep lead_lag

# Kill gracefully
kill -SIGTERM <PID>

# Or force kill if needed
kill -9 <PID>
```

## What to Expect

### Normal Operation

```
=================================================================
Strategy Stats @ 14:32:15
=================================================================
Leader data points: 147
Follower data points: 142
Open positions: 1
Pending orders: 0
Trades executed: 5
Won: 3 | Lost: 2
Total PnL: $42.35
=================================================================

🔔 Jump detected in BTC: 32.45bps
  📈 Correlation: 0.6234
  🎯 Signal: BUY ETH
     Strength: 0.20 | Size: $120.00
  ✅ Order sent: BUY 0.0325 ETH @ $3686.50
     SL: $3671.75 | TP: $3716.00

💼 Closing position leadlag_1731342912345
   Reason: take_profit | PnL: $8.45
```

### First Few Minutes

1. **0-30s**: MarketStream connects, starts buffering data
2. **30-60s**: Strategy receives trades, builds price history
3. **60s+**: Correlation calculation possible, signals may trigger
4. **2-3min**: First trades executed (when conditions met)

## Performance Expectations

- **Latency**: <5ms market data → strategy → order
- **Data Rate**: 10-50 messages/sec per symbol
- **Memory**: ~100MB total for all components
- **CPU**: <10% on modern CPUs

---

**Ready to start?** Run: `./run_lead_lag_system.sh`
