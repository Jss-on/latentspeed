# Hyperliquid Exchange Integration

## Overview

This document describes the Hyperliquid exchange integration for real-time market data streaming in the latentspeed C++ trading engine.

## WebSocket API Details

- **Exchange**: Hyperliquid
- **WebSocket URL**: `wss://api.hyperliquid.xyz/ws`
- **Host**: `api.hyperliquid.xyz`
- **Port**: `443`
- **Target**: `/ws`
- **Documentation**: https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/websocket

## Supported Features

### Market Data Channels

1. **Trades** (`trades`)
   - Real-time trade executions
   - Fields: price (`px`), size (`sz`), side, timestamp, trade ID (`tid`)

2. **Order Book** (`l2Book`)
   - Level 2 order book snapshots
   - Top 10 bid/ask levels
   - Fields: price (`px`), size (`sz`), number of orders (`n`)

## Symbol Format

Hyperliquid uses simple coin symbols without pair notation:
- `BTC` (not BTC-USD or BTCUSDT)
- `ETH`
- `SOL`
- `AVAX`
- etc.

The integration automatically normalizes symbols by:
1. Removing separators (`-`)
2. Converting to uppercase
3. Removing common suffixes (`USDT`, `USD`, `PERP`)

### Examples:
- `"BTC-USDT"` → `"BTC"`
- `"eth-usd"` → `"ETH"`
- `"SOLUSDT"` → `"SOL"`
- `"avax-perp"` → `"AVAX"`

## Message Format

### Subscription Request

```json
{
  "method": "subscribe",
  "subscription": {
    "type": "trades",
    "coin": "BTC"
  }
}
```

### Trade Message

```json
{
  "channel": "trades",
  "data": [
    {
      "coin": "BTC",
      "side": "B",
      "px": "50000.0",
      "sz": "0.5",
      "hash": "0x...",
      "time": 1697234567890,
      "tid": 123456,
      "users": ["0xbuyer", "0xseller"]
    }
  ]
}
```

- **side**: `"B"` = Buy (bid), `"A"` = Ask (sell)
- **px**: Price as string
- **sz**: Size as string
- **time**: Timestamp in milliseconds
- **tid**: Unique trade ID

### Order Book Message

```json
{
  "channel": "l2Book",
  "data": {
    "coin": "BTC",
    "levels": [
      [
        { "px": "50000.0", "sz": "1.5", "n": 3 },
        { "px": "49999.0", "sz": "2.0", "n": 5 }
      ],
      [
        { "px": "50001.0", "sz": "1.2", "n": 2 },
        { "px": "50002.0", "sz": "3.0", "n": 4 }
      ]
    ],
    "time": 1697234567890
  }
}
```

- **levels[0]**: Bids array (descending price)
- **levels[1]**: Asks array (ascending price)
- **px**: Price level
- **sz**: Total size at this level
- **n**: Number of orders

## Usage Example

### C++ Code

```cpp
#include "exchange_interface.h"
#include "market_data_provider.h"

using namespace latentspeed;

// Create Hyperliquid exchange instance
auto exchange = ExchangeFactory::create("hyperliquid");

// Configure exchange
ExchangeConfig config;
config.name = "hyperliquid";
config.symbols = {"BTC", "ETH", "SOL"};
config.enable_trades = true;
config.enable_orderbook = true;

// Generate subscription message
std::string subscription = exchange->generate_subscription(
    config.symbols,
    config.enable_trades,
    config.enable_orderbook
);

// Use with MarketDataProvider
auto provider = std::make_unique<MarketDataProvider>(config);
provider->start();
```

### Configuration File (YAML)

```yaml
exchanges:
  - name: hyperliquid
    symbols:
      - BTC
      - ETH
      - SOL
      - AVAX
    enable_trades: true
    enable_orderbook: true
    snapshots_only: true
    snapshot_interval: 1
    reconnect_attempts: 10
    reconnect_delay_ms: 5000
```

## Implementation Details

### Class: `HyperliquidExchange`

**Header**: `include/exchange_interface.h`  
**Implementation**: `src/exchange_interface.cpp`

#### Key Methods

1. **`get_websocket_host()`** → `"api.hyperliquid.xyz"`
2. **`get_websocket_port()`** → `"443"`
3. **`get_websocket_target()`** → `"/ws"`
4. **`normalize_symbol(symbol)`** → Removes suffixes, converts to uppercase
5. **`generate_subscription(symbols, enable_trades, enable_orderbook)`** → Generates JSON array of subscription messages
6. **`parse_message(message, tick, snapshot)`** → Parses WebSocket messages into `MarketTick` or `OrderBookSnapshot`

### Message Type Handling

| Message Type | Return Value | Description |
|--------------|--------------|-------------|
| Trade | `MessageType::TRADE` | Real-time trade execution |
| Order Book | `MessageType::BOOK` | L2 order book snapshot |
| Subscription Response | `MessageType::HEARTBEAT` | Subscription acknowledgment |
| Parse Error | `MessageType::ERROR` | Invalid JSON or missing fields |
| Unknown | `MessageType::UNKNOWN` | Unrecognized message format |

## Testing

### Manual Test with wscat

```bash
# Install wscat
npm install -g wscat

# Connect to Hyperliquid WebSocket
wscat -c wss://api.hyperliquid.xyz/ws

# Subscribe to BTC trades
> { "method": "subscribe", "subscription": { "type": "trades", "coin": "BTC" } }

# Subscribe to ETH order book
> { "method": "subscribe", "subscription": { "type": "l2Book", "coin": "ETH" } }
```

### C++ Unit Test Example

```cpp
TEST(HyperliquidExchangeTest, SymbolNormalization) {
    HyperliquidExchange exchange;
    
    EXPECT_EQ(exchange.normalize_symbol("BTC-USDT"), "BTC");
    EXPECT_EQ(exchange.normalize_symbol("eth-usd"), "ETH");
    EXPECT_EQ(exchange.normalize_symbol("SOLUSDT"), "SOL");
    EXPECT_EQ(exchange.normalize_symbol("avax-perp"), "AVAX");
}

TEST(HyperliquidExchangeTest, TradeMessageParsing) {
    HyperliquidExchange exchange;
    MarketTick tick;
    OrderBookSnapshot snapshot;
    
    std::string message = R"({
        "channel": "trades",
        "data": [{
            "coin": "BTC",
            "side": "B",
            "px": "50000.0",
            "sz": "0.5",
            "tid": 123456,
            "time": 1697234567890
        }]
    })";
    
    auto result = exchange.parse_message(message, tick, snapshot);
    
    EXPECT_EQ(result, ExchangeInterface::MessageType::TRADE);
    EXPECT_EQ(tick.exchange, "HYPERLIQUID");
    EXPECT_EQ(tick.symbol, "BTC");
    EXPECT_EQ(tick.price, 50000.0);
    EXPECT_EQ(tick.amount, 0.5);
    EXPECT_EQ(tick.side, "buy");
}
```

## Performance Considerations

- **Low Latency**: Hyperliquid operates on a custom L1 blockchain with sub-second block times
- **High Throughput**: Supports high-frequency trading with minimal latency
- **Message Batching**: Uses batched updates for efficiency
- **Snapshot Feed**: Order book is pushed on blocks at least 0.5s apart

## Additional Channels (Future Support)

The following channels are available in the Hyperliquid API but not yet implemented:

- **allMids**: All mid prices across all markets
- **candle**: OHLCV candles (1m, 5m, 1h, etc.)
- **bbo**: Best bid/offer updates only
- **activeAssetCtx**: Asset context (funding, open interest, oracle price)

To add support, extend the `generate_subscription()` and `parse_message()` methods.

## References

- [Hyperliquid WebSocket API](https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/websocket)
- [Hyperliquid Subscriptions](https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/websocket/subscriptions)
- [Python SDK Reference](https://github.com/hyperliquid-dex/hyperliquid-python-sdk)

## Notes

- Hyperliquid is a perpetual futures exchange (not spot)
- All markets are against USD collateral
- No authentication required for public market data streams
- For trading API, see separate trading integration documentation
