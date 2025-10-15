# Latentspeed Trading Engine Examples

This directory contains examples demonstrating how to use the Latentspeed Trading Engine for both market data streaming and order execution.

## Prerequisites

1. **Build the Project**: Ensure the project is built
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```

2. **Python Dependencies** (for Python examples): Install required Python packages
   ```bash
   pip install -r requirements.txt
   ```

## Available Examples

### C++ Examples (Market Data Streaming)

#### hyperliquid_example.cpp - Hyperliquid Exchange Integration
Demonstrates market data streaming from Hyperliquid exchange:

```bash
# Build and run
./build/hyperliquid_example
```

**Features:**
- Basic Hyperliquid connection setup
- Subscription message generation for trades and orderbook
- Message parsing examples (trades and L2 orderbook)
- Symbol normalization (BTC-USDT → BTC)
- Live market data streaming setup

**Supported Symbols:** BTC, ETH, SOL, AVAX, ARB (perpetuals)

See `docs/HYPERLIQUID_INTEGRATION.md` for full documentation.

### Python Examples (Order Execution)

### 1. send_order.py - Basic Order Sender
A comprehensive order sending utility with multiple features:

```bash
# Run test sequence (places, cancels, and modifies orders)
python send_order.py

# Place a specific limit order
python send_order.py --action place --symbol BTCUSDT --side buy --type limit --size 0.001 --price 50000

# Place a market order
python send_order.py --action place --symbol ETHUSDT --side sell --type market --size 0.01

# Cancel an order
python send_order.py --action cancel --cancel-id order_123_456

# Modify an order
python send_order.py --action replace --replace-id order_123_456 --new-price 51000 --new-size 0.002
```

**Features:**
- Support for all order actions (place, cancel, replace)
- Market and limit orders
- Spot and perpetual products
- Command-line interface for easy testing
- Test sequence mode for quick verification

### 2. monitor_reports.py - Execution Report Monitor
Monitors and displays execution reports and fills from the trading engine:

```bash
python monitor_reports.py
```

**Features:**
- Real-time monitoring of execution reports
- Fill notifications with price and size
- Color-coded output for different statuses
- Continuous monitoring until interrupted

### 3. sma_trading_strategy.py - SMA Crossover Strategy
A complete trading strategy implementation using Simple Moving Average crossover:

```bash
# Run with default settings (BTC, 10/30 SMA, testnet)
python sma_trading_strategy.py

# Custom configuration
python sma_trading_strategy.py --symbol ETHUSDT --short-period 5 --long-period 20 --duration 120

# Use market orders instead of limit
python sma_trading_strategy.py --market-orders

# Run on mainnet (requires real API credentials)
python sma_trading_strategy.py --mainnet

# Enable debug logging
python sma_trading_strategy.py --debug
```

**Features:**
- **Market Data Integration**: Fetches real-time prices from Bybit API
- **SMA Calculation**: Computes short and long-term moving averages
- **Signal Generation**: Detects bullish/bearish crossovers
- **Order Management**: Sends orders to trading engine based on signals
- **Position Tracking**: Maintains internal position state
- **Risk Management**: Configurable position sizes and confidence-based sizing
- **Performance Tracking**: Monitors PnL and trade statistics

**Strategy Parameters:**
- `--symbol`: Trading symbol (default: BTCUSDT)
- `--short-period`: Short SMA period (default: 10)
- `--long-period`: Long SMA period (default: 30)
- `--max-size`: Maximum position size (default: 0.001)
- `--duration`: Runtime in minutes (default: 60)
- `--interval`: Update interval in seconds (default: 10)

## Architecture

```
Python Strategy/Client
        │
        ├── ZeroMQ PUSH ──→ Trading Engine (tcp://127.0.0.1:5601)
        │   (ExecutionOrders)
        │
        └── ZeroMQ SUB  ←── Trading Engine (tcp://127.0.0.1:5602)
            (ExecutionReports & Fills)
```

## Message Formats

### ExecutionOrder (Client → Engine)
```json
{
    "version": 1,
    "cl_id": "unique_order_id",
    "action": "place",
    "venue_type": "cex",
    "venue": "bybit",
    "product_type": "perpetual",
    "details": {
        "symbol": "BTCUSDT",
        "side": "buy",
        "order_type": "limit",
        "size": "0.001",
        "price": "50000.0",
        "time_in_force": "GTC"
    },
    "ts_ns": 1234567890000000000,
    "tags": {
        "strategy": "sma_crossover",
        "signal_confidence": "0.85"
    }
}
```

### ExecutionReport (Engine → Client)
```json
{
    "version": 1,
    "cl_id": "unique_order_id",
    "status": "accepted",
    "exchange_order_id": "bybit_12345",
    "reason_code": "ok",
    "reason_text": "Order accepted",
    "ts_ns": 1234567890000000000,
    "tags": {}
}
```

### Fill Report (Engine → Client)
```json
{
    "cl_id": "unique_order_id",
    "exchange_order_id": "bybit_12345",
    "exec_id": "exec_67890",
    "symbol_or_pair": "BTCUSDT",
    "price": 50005.0,
    "size": 0.001,
    "fee_currency": "USDT",
    "fee_amount": 0.05,
    "liquidity": "taker",
    "ts_ns": 1234567890000000000,
    "tags": {}
}
```

## Development Tips

### Testing Your Strategy

1. **Start with Testnet**: Always test on Bybit testnet first
2. **Use Small Sizes**: Start with minimum position sizes
3. **Monitor Logs**: Watch both strategy and engine logs
4. **Check Reports**: Verify execution reports match expectations

### Extending the Examples

1. **Custom Strategies**: Use `sma_trading_strategy.py` as a template
2. **New Indicators**: Add technical indicators to the strategy
3. **Risk Management**: Implement stop-loss and take-profit logic
4. **Multiple Symbols**: Extend to trade multiple pairs simultaneously

### Common Issues

1. **Connection Refused**: Ensure trading engine is running
2. **Order Rejected**: Check symbol format (e.g., BTCUSDT not BTC/USDT)
3. **No Fills**: For testnet, ensure using testnet API endpoints
4. **Import Errors**: Install dependencies with `pip install -r requirements.txt`

## Safety Guidelines

⚠️ **Important Safety Considerations:**

1. **Testnet First**: Always test strategies on testnet before mainnet
2. **Position Limits**: Set appropriate max position sizes
3. **Error Handling**: Implement proper error handling in production
4. **Monitor Actively**: Always monitor your strategies when running
5. **Kill Switch**: Have a way to quickly stop all trading

## License

See the main project LICENSE file.