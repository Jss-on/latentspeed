# Strategy Logging Guide

## Overview

The momentum strategy now uses Python's `logging` module instead of `print()` statements, providing:
- ✅ **Structured logging** with timestamps and log levels
- ✅ **Complete order report data** logged at different verbosity levels
- ✅ **Configurable log levels** via command line
- ✅ **Better debugging** with DEBUG level for raw data

## Log Levels

### INFO (Default)
Shows normal operation:
- Strategy initialization
- Trading signals (BUY/SELL)
- Order submissions
- Order reports (CREATED, FILLED, CANCELLED, etc.)
- Periodic stats (every 30 seconds)

### DEBUG
Shows detailed information:
- Raw ZMQ report data (full JSON)
- Order IDs and confirmation messages
- Complete report payloads

### WARNING
Shows potential issues:
- Order send queue full
- Order rejections
- Order cancellations
- Unknown event types

### ERROR
Shows critical errors:
- Order send failures
- Trade processing errors
- Report processing errors

## Usage

### Basic (INFO level)
```bash
python3 examples/strategy_simple_momentum.py --symbol BTC
```

### Debug Mode (see all data)
```bash
python3 examples/strategy_simple_momentum.py --symbol BTC --log-level DEBUG
```

### Quiet Mode (warnings and errors only)
```bash
python3 examples/strategy_simple_momentum.py --symbol BTC --log-level WARNING
```

## Example Output

### INFO Level
```
2025-11-10 22:15:30 [INFO] Strategy initialized for BTC
2025-11-10 22:15:30 [INFO] Position size: 0.001, Max: 0.01
2025-11-10 22:15:30 [INFO] Momentum window: 20, Threshold: 0.0005
2025-11-10 22:15:30 [INFO] Strategy starting - Listening for BTC trades
2025-11-10 22:15:45 [INFO] 📈 BUY signal - Momentum: 0.0006
2025-11-10 22:15:45 [INFO] Order sent: BUY 0.001 BTC @ $50000.00
2025-11-10 22:15:46 [INFO] ✅ ORDER CREATED - BUY 0.001 BTC-USD @ $50000.00
2025-11-10 22:15:46 [INFO]    Client ID: momentum_1699999999000 | Exchange ID: 123456
2025-11-10 22:15:47 [INFO] 💰 ORDER FILLED - BUY 0.001 BTC-USD @ $50000.00
2025-11-10 22:15:47 [INFO]    Client ID: momentum_1699999999000 | Exchange ID: 123456
2025-11-10 22:16:00 [INFO] 📊 Stats - Position: 0.0010 | Price: $50123.45 | Momentum: 0.0003 | Orders: 1
```

### DEBUG Level
```
2025-11-10 22:15:45 [DEBUG] Order ID: momentum_1699999999000 - Waiting for confirmation...
2025-11-10 22:15:46 [DEBUG] Raw report data: {
  "event_type": "CREATED",
  "client_order_id": "momentum_1699999999000",
  "exchange_order_id": "123456",
  "symbol": "BTC-USD",
  "side": "buy",
  "price": 50000.0,
  "quantity": 0.001,
  "timestamp": 1699999999000
}
2025-11-10 22:15:46 [INFO] ✅ ORDER CREATED - BUY 0.001 BTC-USD @ $50000.00
2025-11-10 22:15:46 [INFO]    Client ID: momentum_1699999999000 | Exchange ID: 123456
2025-11-10 22:15:46 [DEBUG]    Full data: {'event_type': 'CREATED', 'client_order_id': ...}
```

## Report Event Types

All order reports are logged with full details:

### ORDER CREATED ✅
```
[INFO] ✅ ORDER CREATED - BUY 0.001 BTC-USD @ $50000.00
[INFO]    Client ID: momentum_1699999999000 | Exchange ID: 123456
[DEBUG]    Full data: {...}
```

### ORDER FILLED 💰
```
[INFO] 💰 ORDER FILLED - BUY 0.001 BTC-USD @ $50000.00
[INFO]    Client ID: momentum_1699999999000 | Exchange ID: 123456
[DEBUG]    Full data: {...}
```

### PARTIAL FILL 📊
```
[INFO] 📊 PARTIAL FILL - Filled: 0.0005, Remaining: 0.0005 @ $50000.00
[INFO]    Client ID: momentum_1699999999000
[DEBUG]    Full data: {...}
```

### ORDER CANCELLED ❌
```
[WARNING] ❌ ORDER CANCELLED - momentum_1699999999000
[INFO]    Exchange ID: 123456
[DEBUG]    Full data: {...}
```

### ORDER REJECTED ⚠️
```
[WARNING] ⚠️  ORDER REJECTED - momentum_1699999999000
[WARNING]    Reason: Insufficient balance
[DEBUG]    Full data: {...}
```

### ORDER FAILED ❌
```
[ERROR] ❌ ORDER FAILED - momentum_1699999999000
[ERROR]    Reason: Invalid price
[DEBUG]    Full data: {...}
```

## Logging to File

To save logs to a file:

```bash
python3 examples/strategy_simple_momentum.py --symbol BTC 2>&1 | tee logs/strategy_$(date +%Y%m%d_%H%M%S).log
```

Or modify the logging configuration in the script:

```python
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S',
    handlers=[
        logging.StreamHandler(),  # Console
        logging.FileHandler('logs/strategy.log')  # File
    ]
)
```

## Complete Command Line Options

```bash
python3 examples/strategy_simple_momentum.py \
    --symbol BTC \              # Trading symbol
    --size 0.001 \              # Position size
    --max-position 0.01 \       # Max total position
    --window 20 \               # Momentum window
    --threshold 0.0005 \        # Momentum threshold (0.05%)
    --log-level DEBUG           # Log level: DEBUG, INFO, WARNING, ERROR
```

## Debugging Tips

### See all order data
```bash
--log-level DEBUG
```

### Track specific order
```bash
--log-level DEBUG | grep "momentum_1699999999000"
```

### Monitor only errors
```bash
--log-level ERROR
```

### Count filled orders
```bash
--log-level INFO | grep "ORDER FILLED" | wc -l
```

### Watch live with timestamps
```bash
--log-level INFO | ts '[%Y-%m-%d %H:%M:%S]'
```

## Benefits Over Print Statements

| Feature | print() | logging |
|---------|---------|---------|
| **Timestamps** | ❌ Manual | ✅ Automatic |
| **Log Levels** | ❌ No | ✅ DEBUG/INFO/WARNING/ERROR |
| **Filtering** | ❌ Hard | ✅ Easy (--log-level) |
| **Formatting** | ❌ Manual | ✅ Consistent |
| **File Output** | ❌ Redirect only | ✅ Built-in handlers |
| **Production Ready** | ❌ No | ✅ Yes |

## Summary

The strategy now provides:
1. ✅ **Professional logging** with proper levels
2. ✅ **Complete order data** logged at DEBUG level
3. ✅ **Configurable verbosity** via --log-level
4. ✅ **Structured output** for parsing and analysis
5. ✅ **Production-ready** logging infrastructure

Perfect for debugging, monitoring, and production deployment! 🎯
