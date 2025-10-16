# MarketStream Testing Scripts

Python test scripts to verify marketstream ZMQ data feeds.

## Prerequisites

```bash
pip install pyzmq
```

## Test Scripts

### 1. simple_zmq_test.py - Quick Connection Test

Minimal test to verify marketstream is working and sending data.

```bash
python tests/simple_zmq_test.py
```

**Output:**
```
======================================================================
Simple MarketStream ZMQ Test
======================================================================
Connecting to ZMQ streams...
✓ Connected to trades: tcp://127.0.0.1:5556
✓ Connected to orderbooks: tcp://127.0.0.1:5557

Listening for data (Ctrl+C to stop)...

[TRADE #1] HYPERLIQUID:BTC - Price: $50123.50, Amount: 1.2500, Side: buy
[BOOK #1] HYPERLIQUID:ETH - Mid: $3045.23, Bid: $3045.00, Ask: $3045.45
[TRADE #2] HYPERLIQUID:SOL - Price: $145.67, Amount: 25.0000, Side: sell
...
```

### 2. test_marketstream_zmq.py - Full Featured Test

Comprehensive test with statistics, formatted output, and filtering options.

```bash
# Default: show both trades and books
python tests/test_marketstream_zmq.py

# Show trades only
python tests/test_marketstream_zmq.py --trades-only

# Show books only
python tests/test_marketstream_zmq.py --books-only

# Quiet mode - only show statistics
python tests/test_marketstream_zmq.py --quiet

# Custom stats interval (default: 10 seconds)
python tests/test_marketstream_zmq.py --stats-interval 5
```

**Output Example:**

```
======================================================================
MarketStream ZMQ Consumer - Listening for data...
Trades: ENABLED
Books: ENABLED
Press Ctrl+C to stop
======================================================================

[TRADE #1] HYPERLIQUID:BTC | BUY  | Price: $ 50,123.50 | Amount:     1.2500

======================================================================
[ORDERBOOK #1] HYPERLIQUID:ETH
Mid: $3,045.23 | Spread: $0.45 (1.48 bps)
======================================================================
BIDS                                | ASKS                               
----------------------------------------------------------------------
$ 3,045.00 x   10.5000              | $ 3,045.45 x    8.3000
$ 3,044.50 x    5.2000              | $ 3,046.00 x   12.1000
$ 3,044.00 x   15.8000              | $ 3,046.50 x    6.7000

======================================================================
Statistics (Running for 10.0s)
======================================================================
Exchange:Symbol           Trades          Books           Rate (msg/s)
----------------------------------------------------------------------
HYPERLIQUID:AVAX          15              20                     3.50
HYPERLIQUID:BTC           25              30                     5.50
HYPERLIQUID:ETH           18              22                     4.00
----------------------------------------------------------------------
TOTAL                     58              72                    13.00
======================================================================
```

## Running Tests

### Step 1: Start MarketStream

In Terminal 1:
```bash
cd build/linux-release
./marketstream ../../configs/config.yml
```

Wait for:
```
[INFO] Streaming market data (Press Ctrl+C to stop)
ZMQ Output:
  - Trades:     tcp://127.0.0.1:5556
  - Orderbooks: tcp://127.0.0.1:5557
```

### Step 2: Run Test Script

In Terminal 2:
```bash
# Quick test
python tests/simple_zmq_test.py

# Full featured test
python tests/test_marketstream_zmq.py
```

## Troubleshooting

### No Data Received

**Problem:**
```
✓ Connected to trades: tcp://127.0.0.1:5556
✓ Connected to orderbooks: tcp://127.0.0.1:5557

Listening for data (Ctrl+C to stop)...
(no output)
```

**Solutions:**
1. Check if marketstream is running: `ps aux | grep marketstream`
2. Verify ZMQ ports are open: `netstat -an | grep 5556`
3. Check marketstream logs for errors: `tail -f marketstream.log`
4. Ensure symbols are configured in `config.yml`

### Connection Refused

**Problem:**
```
zmq.error.ZMQError: Connection refused
```

**Solution:** Start marketstream first before running test scripts.

### Import Error

**Problem:**
```
ModuleNotFoundError: No module named 'zmq'
```

**Solution:**
```bash
pip install pyzmq
```

## Testing Specific Exchanges

### Hyperliquid Only

Edit `config.yml`:
```yaml
feeds:
  - exchange: hyperliquid
    symbols: [BTC, ETH, SOL]
    enable_trades: true
    enable_orderbook: true
```

Then run test:
```bash
python tests/test_marketstream_zmq.py
```

### Multi-Exchange

Enable multiple exchanges in `config.yml`:
```yaml
feeds:
  - exchange: hyperliquid
    symbols: [BTC, ETH]
  - exchange: dydx
    symbols: [BTC-USD, ETH-USD]
```

The test will show data from all configured exchanges.

## Advanced Usage

### Filter by Exchange in Test Script

Modify `test_marketstream_zmq.py` to filter:

```python
# In the run() method, add filter:
if trade_data.get('exchange') == 'HYPERLIQUID':
    print(self.format_trade(trade_data))
```

### Save Data to File

```bash
python tests/test_marketstream_zmq.py > market_data.log 2>&1
```

### Monitor Message Rate

```bash
python tests/test_marketstream_zmq.py --quiet
```

Shows only statistics, useful for monitoring system performance.

## Data Format

### Trade Message

```json
{
  "exchange": "HYPERLIQUID",
  "symbol": "BTC",
  "price": 50123.50,
  "amount": 1.25,
  "side": "buy",
  "trade_id": "123456",
  "timestamp_ns": 1697234567890000000,
  "seq": 1
}
```

### OrderBook Message

```json
{
  "exchange": "HYPERLIQUID",
  "symbol": "ETH",
  "timestamp_ns": 1697234567890000000,
  "seq": 1,
  "bids": [
    {"price": 3045.00, "quantity": 10.5},
    {"price": 3044.50, "quantity": 5.2}
  ],
  "asks": [
    {"price": 3045.45, "quantity": 8.3},
    {"price": 3046.00, "quantity": 12.1}
  ],
  "midpoint": 3045.225,
  "relative_spread": 0.000148
}
```

## Performance Testing

Monitor message throughput:

```bash
# Run test and count messages
python tests/test_marketstream_zmq.py --quiet --stats-interval 1
```

Expected performance:
- **Hyperliquid**: 5-20 messages/second per symbol
- **dYdX**: 10-50 messages/second per symbol
- **Bybit**: 20-100 messages/second per symbol

## Integration Examples

### Use in Your Trading Bot

```python
import zmq
import json

context = zmq.Context()
trades = context.socket(zmq.SUB)
trades.connect("tcp://127.0.0.1:5556")
trades.setsockopt_string(zmq.SUBSCRIBE, "")

while True:
    trade = trades.recv_json()
    
    # Your trading logic here
    if trade['exchange'] == 'HYPERLIQUID' and trade['symbol'] == 'BTC':
        price = trade['price']
        # Execute strategy...
```

### Async Consumer

```python
import asyncio
import zmq.asyncio

async def consume_trades():
    context = zmq.asyncio.Context()
    socket = context.socket(zmq.SUB)
    socket.connect("tcp://127.0.0.1:5556")
    socket.subscribe("")
    
    while True:
        trade = await socket.recv_json()
        await process_trade(trade)

asyncio.run(consume_trades())
```

## Next Steps

- Use this data in your trading strategies
- Connect to trading engine for order execution
- Build real-time analytics dashboard
- Implement cross-exchange arbitrage

See `examples/sma_trading_strategy.py` for a complete trading strategy example.
