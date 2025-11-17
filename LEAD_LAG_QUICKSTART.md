# Lead-Lag Trading System - Quick Start Guide

Complete integration of **MarketStream → Lead-Lag Strategy → Trading Engine** for Hyperliquid perpetuals.

## Architecture

```
┌──────────────┐      ┌──────────────┐      ┌─────────────────┐
│ MarketStream │─────▶│  Lead-Lag    │─────▶│ Trading Engine  │
│   (C++)      │ ZMQ  │  Strategy    │ ZMQ  │    (C++)        │
│              │ 5556 │  (Python)    │ 5601 │                 │
│  BTC/ETH     │ 5557 │              │ 5602 │  Hyperliquid    │
│  Hyperliquid │      │  Correlation │      │  Execution      │
└──────────────┘      └──────────────┘      └─────────────────┘
```

## What It Does

**Lead-Lag Strategy** monitors BTC (leader) and ETH (follower):

1. **Detects Jumps**: When BTC price jumps >25bps
2. **Calculates Correlation**: Analyzes BTC/ETH correlation over 100 ticks
3. **Trades ETH**: If correlation >0.55, trades ETH in predicted direction
4. **Risk Management**: 
   - Stop Loss: 40bps (0.4%)
   - Take Profit: 80bps (0.8%)
   - Max Hold Time: 120 seconds
   - Max Positions: 2 concurrent

## Prerequisites

1. **Built C++ binaries** (marketstream, trading_engine_service)
2. **Python 3** with packages: `pyzmq`, `numpy`
3. **Hyperliquid credentials** (already configured in script)

## Quick Start

### 1. Make script executable

```bash
chmod +x run_lead_lag_system.sh
```

### 2. Build the system (if not already built)

```bash
./run.sh --release
```

### 3. Run the complete system

```bash
./run_lead_lag_system.sh
```

This will:
- ✅ Build C++ components
- ✅ Start MarketStream (Hyperliquid BTC/ETH data)
- ✅ Start Trading Engine (Hyperliquid execution)
- ✅ Start Lead-Lag Strategy (Python)
- ✅ Show live strategy output

### 4. Monitor the system

The script automatically tails the strategy log. You'll see:

```
🔔 Jump detected in BTC: 32.45bps
  📈 Correlation: 0.6234
  🎯 Signal: BUY ETH
     Strength: 0.20 | Size: $120.00
  ✅ Order sent: BUY 0.0325 ETH @ $3686.50
     SL: $3671.75 | TP: $3716.00
```

### 5. Stop the system

Press `Ctrl+C` - all components will shut down gracefully.

## Files Created

| File | Purpose |
|------|---------|
| `configs/hyperliquid_credentials.yml` | Hyperliquid wallet credentials |
| `examples/lead_lag_strategy_live.py` | Main strategy implementation |
| `run_lead_lag_system.sh` | Orchestration script |
| `logs/marketstream.log` | Market data logs |
| `logs/trading_engine.log` | Trading engine logs |
| `logs/strategy.log` | Strategy logs |

## Configuration

Edit `examples/lead_lag_strategy_live.py` to adjust:

```python
config = {
    "leader_symbol": "BTC",
    "follower_symbol": "ETH",
    "jump_threshold_bps": 25.0,       # Jump sensitivity
    "min_correlation": 0.55,          # Correlation threshold
    "base_position_usd": 100.0,       # Base position size
    "max_position_usd": 300.0,        # Max position size
    "stop_loss_bps": 40.0,            # Stop loss
    "take_profit_bps": 80.0,          # Take profit
    "max_open_positions": 2,          # Max concurrent positions
    "position_timeout_seconds": 120   # Max hold time
}
```

## Monitoring

### Real-time logs

```bash
# MarketStream
tail -f logs/marketstream.log

# Trading Engine  
tail -f logs/trading_engine.log

# Strategy
tail -f logs/strategy.log
```

### Test individual components

```bash
# Test MarketStream only
./build/marketstream --config configs/marketstream_hyperliquid.yml

# Test ZMQ data reception
python3 examples/test_marketstream_zmq.py

# Test Trading Engine only
./build/trading_engine_service --exchange hyperliquid --api-key $HYPERLIQUID_USER_ADDRESS --api-secret $HYPERLIQUID_PRIVATE_KEY --live
```

## Safety Features

1. **Position Limits**: Max 2 concurrent positions
2. **Size Limits**: $100-$300 per trade
3. **Stop Loss**: Automatic at 0.4% loss
4. **Take Profit**: Automatic at 0.8% gain
5. **Timeout**: Positions auto-close after 120 seconds
6. **Correlation Gate**: Only trades when correlation >0.55

## Troubleshooting

### MarketStream not receiving data

```bash
# Check Hyperliquid connectivity
curl https://api.hyperliquid.xyz/info

# Verify ZMQ ports
netstat -an | grep 555
```

### Trading Engine not connecting

```bash
# Check credentials
echo $HYPERLIQUID_USER_ADDRESS
echo $HYPERLIQUID_PRIVATE_KEY

# View detailed logs
tail -100 logs/trading_engine.log
```

### Strategy not seeing data

```bash
# Test ZMQ connection
python3 examples/test_marketstream_zmq.py --trades-only

# Check Python dependencies
python3 -c "import zmq, numpy; print('OK')"
```

### No trades executing

- **Low correlation**: BTC/ETH correlation below threshold (need >0.55)
- **Small jumps**: BTC price jumps below 25bps threshold
- **Position limits**: Already at max 2 positions
- **Insufficient data**: Need >10 price points to calculate correlation

## Performance Metrics

The strategy prints statistics every 30 seconds:

```
==============================================================
Strategy Stats @ 14:32:15
==============================================================
Leader data points: 147
Follower data points: 142
Open positions: 1
Pending orders: 0
Trades executed: 5
Won: 3 | Lost: 2
Total PnL: $42.35
==============================================================
```

## Next Steps

1. **Backtest**: Use historical data to optimize parameters
2. **Add pairs**: Extend to SOL/BTC, AVAX/ETH, etc.
3. **Advanced signals**: Implement Hayashi-Yoshida or mutual information estimators
4. **Risk controls**: Add daily loss limits, drawdown monitoring
5. **Web dashboard**: Build real-time monitoring UI

## Advanced: Integration with arkpad-ahab2

This is a simplified version. For the full adaptive lead-lag strategy from arkpad-ahab2:

```bash
cd sub/arkpad-ahab2
# Follow trading_core integration guide in README.md
```

---

**Ready to trade?** Run `./run_lead_lag_system.sh`
