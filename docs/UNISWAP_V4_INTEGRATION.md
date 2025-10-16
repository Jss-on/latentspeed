# Uniswap V4 DEX Integration Guide

## Overview

Uniswap V4 integration allows you to stream **on-chain DEX data** from Ethereum mainnet, including:
- **Swap events** (trades) from liquidity pools
- **Liquidity modifications** (pool state changes)
- **Synthetic orderbook** generation from AMM reserves

**Key Difference from CEX:** Unlike centralized exchanges (Bybit, Binance, etc.), Uniswap operates on-chain using Automated Market Maker (AMM) mechanics rather than traditional orderbooks.

## Architecture

```
Ethereum Node (Infura/Alchemy)
          ↓ (WebSocket)
    UniswapV4Exchange
          ↓
   JSON-RPC eth_subscribe
          ↓
   Event Logs (Swap, ModifyLiquidity)
          ↓
    Parse & Decode ABI
          ↓
 MarketStream → ZMQ (5556/5557)
```

## Prerequisites

### 1. Ethereum Node Access

You need access to an Ethereum node via WebSocket. Options:

**Infura (Recommended for testing):**
```bash
# Sign up at https://infura.io
# Get your API key
export INFURA_API_KEY="your_key_here"

# WebSocket endpoint:
wss://mainnet.infura.io/ws/v3/YOUR_API_KEY
```

**Alchemy:**
```bash
# Sign up at https://alchemy.com
# Get your API key
export ALCHEMY_API_KEY="your_key_here"

# WebSocket endpoint:
wss://eth-mainnet.g.alchemy.com/v2/YOUR_API_KEY
```

**Local Node:**
```bash
# Run Geth or Erigon with WebSocket enabled
geth --ws --ws.addr="0.0.0.0" --ws.port=8546 --ws.api="eth,net,web3"
```

### 2. Uniswap V4 Contract Addresses

Uniswap V4 is currently on testnet. Update these addresses when mainnet launches:

```cpp
// In src/exchange_interface.cpp, update:
const std::string POOL_MANAGER_ADDRESS = "0x...";  // V4 PoolManager
```

## Configuration

### Basic Config

Add to `configs/config.yml`:

```yaml
feeds:
  - exchange: uniswapv4
    symbols:
      - WETH/USDC    # WETH-USDC 0.05% pool
      - WETH/USDT    # WETH-USDT 0.05% pool
      - WBTC/WETH    # WBTC-WETH 0.3% pool
    enable_trades: true
    enable_orderbook: true
    snapshots_only: true
    snapshot_interval: 1
```

### Configure Ethereum Node

**Option 1: Environment Variable**
```bash
export ETH_WS_URL="wss://mainnet.infura.io/ws/v3/YOUR_KEY"
```

**Option 2: Code Modification**

Edit `include/exchange_interface.h`:

```cpp
std::string get_websocket_target() const override {
    return "/ws/v3/YOUR_INFURA_KEY";  // Replace with your key
}
```

## Symbol Format

Uniswap V4 uses **token pair** notation:

### Supported Formats
```
WETH/USDC   ✓ Canonical format
WETH-USDC   ✓ Converts to WETH/USDC
weth/usdc   ✓ Converts to WETH/USDC
ETH/USDC    ✓ Converts to WETH/USDC (auto-wrapped)
```

### Common Pairs
```yaml
symbols:
  - WETH/USDC      # Most liquid stablecoin pair
  - WETH/USDT      # Alternative stablecoin
  - WETH/DAI       # Decentralized stablecoin
  - WBTC/WETH      # Wrapped Bitcoin
  - WBTC/USDC      # BTC to stablecoin
  - UNI/WETH       # Uniswap token
  - LINK/WETH      # Chainlink
  - AAVE/WETH      # Aave
```

## Event Types

### 1. Swap Events (Trades)

**Event Signature:**
```
Swap(address indexed sender, address indexed recipient, 
     int256 amount0, int256 amount1, uint160 sqrtPriceX96, 
     uint128 liquidity, int24 tick)
```

**Decoded Fields:**
- `amount0`: Token0 amount changed (positive = pool received, negative = pool sent)
- `amount1`: Token1 amount changed
- `sqrtPriceX96`: Current pool price (Q64.96 format)
- `liquidity`: Pool liquidity
- `tick`: Current price tick

**Price Calculation:**
```cpp
// sqrtPriceX96 to actual price
double price = pow(sqrtPriceX96 / pow(2, 96), 2);

// Adjust for token decimals
price = price * pow(10, token0_decimals - token1_decimals);
```

### 2. ModifyLiquidity Events (Pool State)

**Event Signature:**
```
ModifyLiquidity(address indexed sender, int24 tickLower, 
                int24 tickUpper, int256 liquidityDelta)
```

**Use Case:**
- Monitor liquidity depth changes
- Generate synthetic orderbook from AMM curve
- Track concentrated liquidity positions

## Synthetic Orderbook Generation

Since Uniswap uses AMM (no native orderbook), we generate synthetic levels:

### Methodology

1. **Get current pool state** via `eth_call`:
   ```json
   {
     "method": "eth_call",
     "params": [{
       "to": "POOL_ADDRESS",
       "data": "0x..." // slot0() function selector
     }]
   }
   ```

2. **Calculate price levels** around current price:
   ```cpp
   // For each tick around current_tick:
   for (int i = -10; i <= 10; i++) {
       int tick = current_tick + (i * tick_spacing);
       double price = 1.0001^tick;  // Uniswap tick formula
       
       // Calculate available liquidity at this tick
       double liquidity = get_liquidity_at_tick(tick);
       
       if (i < 0) bids.push_back({price, liquidity});
       else asks.push_back({price, liquidity});
   }
   ```

3. **Convert to orderbook format**:
   ```json
   {
     "bids": [[3045.50, 10.5], [3045.00, 8.3], ...],
     "asks": [[3046.00, 12.1], [3046.50, 6.7], ...]
   }
   ```

## Implementation Status

### ✅ Implemented
- Exchange interface structure
- WebSocket connection to Ethereum node
- JSON-RPC subscription generation
- Event signature definitions
- Basic message parsing framework

### ⚠️ TODO (Production Ready)
1. **ABI Decoding:** Full Solidity ABI decoder for event parameters
2. **Pool Resolution:** Map pool IDs to token pairs
3. **Price Calculation:** Implement sqrtPriceX96 → price conversion
4. **Token Decimals:** Handle different decimal precisions
5. **Liquidity Curves:** Generate synthetic orderbook from AMM formula
6. **Multi-Pool Support:** Track multiple pools per pair (different fee tiers)
7. **Block Confirmations:** Handle chain reorganizations
8. **Gas Price Tracking:** Monitor MEV and gas costs

## Production Deployment

### 1. ABI Decoding Library

Add to `vcpkg.json`:
```json
{
  "dependencies": [
    "ethash",
    "intx"
  ]
}
```

Or use external library:
- **ethabi-cpp**: Ethereum ABI encoder/decoder
- **web3cpp**: Complete Web3 C++ library

### 2. Pool Mapping

Create pool registry:
```cpp
// Map pool addresses to token pairs
std::unordered_map<std::string, TokenPair> pool_registry {
    {"0x88e6a0c2ddd26feeb64f039a2c41296fcb3f5640", {"WETH", "USDC", 500}},
    {"0x4e68ccd3e89f51c3074ca5072bbac773960dfa36", {"WETH", "USDT", 500}},
    // ... more pools
};
```

### 3. Multi-Node Redundancy

```yaml
ethereum_nodes:
  - url: wss://mainnet.infura.io/ws/v3/KEY1
    priority: 1
  - url: wss://eth-mainnet.g.alchemy.com/v2/KEY2
    priority: 2
  - url: ws://localhost:8546
    priority: 3
```

### 4. Archive Node (Historical Data)

For backtesting, use archive node:
```bash
# Infura archive node
wss://mainnet.infura.io/ws/v3/KEY_WITH_ARCHIVE_ACCESS

# Query historical state
eth_call ... "latest|0x..." # block hash or number
```

## Testing

### Local Testnet

1. **Run Anvil (Foundry local node):**
   ```bash
   anvil --fork-url https://mainnet.infura.io/v3/YOUR_KEY
   ```

2. **Connect marketstream:**
   ```yaml
   feeds:
     - exchange: uniswapv4
       endpoint: ws://localhost:8545  # Anvil default
       symbols: [WETH/USDC]
   ```

### Monitor Events

```bash
# Watch Uniswap events in real-time
cast logs \
  --address 0x88e6a0c2ddd26feeb64f039a2c41296fcb3f5640 \
  --from-block latest \
  --subscribe
```

## Performance Considerations

### 1. Event Volume

Uniswap V4 generates **high event volume** on mainnet:
- Peak: 50-200 Swap events per block
- Block time: ~12 seconds
- Expected rate: 5-20 events/second per pool

### 2. Memory Usage

Each event log ~500 bytes:
- 10 pools × 20 events/sec = 200 events/sec
- Hourly: 720,000 events × 500 bytes = ~350 MB

Ensure sufficient buffer sizes in `market_data_provider.h`.

### 3. Latency

- **WebSocket lag:** 100-500ms from block creation
- **Infura/Alchemy:** 200-800ms average
- **Local node:** 50-200ms (< 0.2s)
- **MEV protection:** Consider using Flashbots RPC

## Advanced Features

### MEV Detection

Monitor for:
- Sandwich attacks (buy → victim → sell pattern)
- Arbitrage opportunities (price differences)
- Frontrunning (pending tx mempool)

```cpp
// Detect sandwich pattern
if (consecutive_swaps.size() >= 3) {
    auto& first = consecutive_swaps[0];
    auto& victim = consecutive_swaps[1];
    auto& last = consecutive_swaps[2];
    
    if (first.sender == last.sender && first.direction != victim.direction) {
        // Potential sandwich attack detected
    }
}
```

### Gas Price Oracle

```cpp
// Subscribe to pending transactions
{
  "method": "eth_subscribe",
  "params": ["newPendingTransactions"]
}

// Calculate gas percentiles
double median_gas = calculate_percentile(gas_prices, 0.5);
double fast_gas = calculate_percentile(gas_prices, 0.9);
```

## Example Usage

See `examples/uniswap_v4_example.cpp` for:
1. Basic connection setup
2. Event subscription
3. Swap parsing
4. Synthetic orderbook generation
5. Multi-pool monitoring

## Troubleshooting

### No Events Received

**Check:**
1. Ethereum node WebSocket connection
2. API key validity
3. Pool address is correct
4. Event signature matches V4 contracts

**Debug:**
```bash
# Test WebSocket connection
wscat -c wss://mainnet.infura.io/ws/v3/YOUR_KEY

# Send test subscription
{"jsonrpc":"2.0","id":1,"method":"eth_blockNumber"}
```

### Incorrect Prices

**Verify:**
1. sqrtPriceX96 conversion
2. Token decimal adjustments
3. Pool token0/token1 ordering

### High Memory Usage

**Solutions:**
1. Filter events by block range
2. Prune old events
3. Use bloom filters for relevant transactions
4. Subscribe to specific pool addresses only

## Resources

- **Uniswap V4 Docs:** https://docs.uniswap.org/contracts/v4
- **Ethereum JSON-RPC:** https://ethereum.org/en/developers/docs/apis/json-rpc/
- **Event Topics:** https://docs.soliditylang.org/en/latest/abi-spec.html#events
- **Infura Docs:** https://docs.infura.io/networks/ethereum/json-rpc-methods
- **Web3 C++ Examples:** https://github.com/ethereum/web3.cpp

## Next Steps

1. **Implement full ABI decoder** for production use
2. **Add pool registry** for symbol mapping
3. **Test with real Ethereum node** (Infura/Alchemy)
4. **Build synthetic orderbook generator**
5. **Add MEV detection module**
6. **Integrate with trading strategies**

For questions or issues, see the main project documentation or file an issue on GitHub.
