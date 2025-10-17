# Hyperliquid Adapter Usage

This page shows how to use the Hyperliquid adapter in Latentspeed, including configuring the Python signer bridge, connecting private WebSocket streams, and placing/canceling orders. It complements:
- latentspeed/docs/HYPERLIQUID_REFERENCE.md (API reference + latency)
- latentspeed/docs/HYPERLIQUID_SIGNER_README.md (signer setup)

## 1) Prerequisites
- Build the project: `cd latentspeed && ./run.sh --release`
- Python 3.10+ available
- Set up the signer bridge and environment variables:
  - `bash latentspeed/tools/setup_hl_signer.sh`
  - `export LATENTSPEED_HL_SIGNER_PYTHON="latentspeed/.venv-hl-signer/bin/python"`
  - `export LATENTSPEED_HL_SIGNER_SCRIPT="latentspeed/tools/hl_signer_bridge.py"`

Notes
- The signer bridge uses the official `hyperliquid-python-sdk` for exact signing parity.
- The engine/adapter never logs secrets and keeps private keys only in the signer process memory.

## 2) Credentials for Hyperliquid
Hyperliquid uses wallet addresses and API (agent) wallets:
- User address (master/subaccount) is used for queries and private streams.
- API wallet private key (agent wallet) signs actions on behalf of the user.

When calling `initialize(api_key, api_secret, testnet)` on the adapter:
- `api_key`: set to the lowercased user address (0x… of master or subaccount).
- `api_secret`: set to the private key (hex, with or without 0x) of the API wallet that is authorized to sign for this user.
- `testnet`: `true` for testnet, `false` for mainnet.

This matches Hyperliquid docs: use the actual user/subaccount address for account state and streams; use the agent key for signing.

## 3) Programmatic usage (C++)
Below is a minimal example showing how to use the adapter directly. This is useful before the engine wiring for Hyperliquid is added to `TradingEngineService`.

```cpp
#include "adapters/hyperliquid_adapter.h"
#include "exchange/exchange_client.h" // OrderRequest/OrderResponse types
#include <iostream>

using namespace latentspeed;

int main() {
    // Create adapter
    HyperliquidAdapter hl;

    // Initialize with: user address, agent wallet private key, testnet flag
    const std::string user_address = "0x...lowercased_user_or_subaccount";
    const std::string agent_private_key = "0x...32byteshex";
    bool testnet = true;
    if (!hl.initialize(user_address, agent_private_key, testnet)) {
        std::cerr << "init failed\n"; return 1;
    }

    // Optional: connect WS (private streams: orderUpdates, userEvents, userFills)
    hl.set_order_update_callback([](const OrderUpdate& u){
        std::cout << "order update: clid=" << u.client_order_id << " status=" << u.status << "\n";
    });
    hl.set_fill_callback([](const FillData& f){
        std::cout << "fill: oid=" << f.exchange_order_id << " px=" << f.price << " sz=" << f.quantity << "\n";
    });
    hl.connect(); // establishes WS and subscribes to private streams

    // Place a limit order (perpetual example)
    OrderRequest req{};
    req.client_order_id = "0x1234567890abcdef1234567890abcdef"; // optional cloid; must be 0x + 32 hex chars
    req.symbol = "BTC";               // perps: base name is enough (adapter resolves asset)
    req.side = "buy";                 // "buy" | "sell"
    req.order_type = "limit";         // only limit supported in example
    req.quantity = "0.01";            // decimal string; adapter trims trailing zeros
    req.price = std::string("65000"); // decimal string
    req.time_in_force = "GTC";        // "GTC" | "IOC" | "PO" (post-only)
    req.category = "perpetual";       // or "spot" for spot markets

    OrderResponse r = hl.place_order(req);
    if (!r.success) {
        std::cerr << "place failed: " << r.message << "\n";
    } else {
        std::cout << "place ok, oid=" << (r.exchange_order_id.value_or("")) << "\n";
    }

    // Cancel by oid (if known) or by cloid (recommended)
    if (r.exchange_order_id) {
        OrderResponse c = hl.cancel_order(req.client_order_id, req.symbol, r.exchange_order_id);
        std::cout << "cancel: " << (c.success ? "ok" : c.message) << "\n";
    }
    return 0;
}
```

Spot example
- For spot, use `req.category = "spot"`.
- Symbols should be "BASE-QUOTE" (e.g., "HYPE-USDC"). The adapter resolves `asset = 10000 + index` via `/info spotMeta`.

## 4) Behavior notes
- Signing: The adapter calls the Python signer bridge to produce r,s,v for L1 actions.
- Transport: Prefers WebSocket `post` to `/exchange`; falls back to HTTP `/exchange` on failure.
- Streams: On `connect()`, subscribes to `orderUpdates`, `userEvents`, and `userFills` and emits callbacks.
- Status normalization: HL status strings are normalized to `new/accepted/filled/canceled/rejected` for engine consumption.
- Error mapping: HL rejections (e.g., `tickRejected`, `badAloPxRejected`, `minTradeNtlRejected`) are mapped to canonical codes in DefaultReasonMapper.

## 5) Environment variables (signer)
- `LATENTSPEED_HL_SIGNER_PYTHON`: path to Python interpreter (recommended: the venv python)
- `LATENTSPEED_HL_SIGNER_SCRIPT`: path to `latentspeed/tools/hl_signer_bridge.py`

If not set, defaults are `python3` and `latentspeed/tools/hl_signer_bridge.py` (relative to your working dir). See the signer README for details.

## 6) Engine wiring
- The `trading_engine_service` executable now wires Bybit, Binance, and Hyperliquid.
- Set `--exchange hyperliquid` and provide credentials via either config or environment:
  - Recommended (DEX semantics):
    - `LATENTSPEED_HYPERLIQUID_USER_ADDRESS` = 0x… lowercased user/subaccount address
    - `LATENTSPEED_HYPERLIQUID_PRIVATE_KEY` = 0x… agent wallet private key (hex)
  - Backward-compatible aliases:
    - `LATENTSPEED_HYPERLIQUID_API_KEY` (mapped to user address)
    - `LATENTSPEED_HYPERLIQUID_API_SECRET` (mapped to private key)
  - Optional:
    - `LATENTSPEED_HYPERLIQUID_USE_TESTNET=1`
  - Signer bridge (if using a venv):
    - `LATENTSPEED_HL_SIGNER_PYTHON`, `LATENTSPEED_HL_SIGNER_SCRIPT`

Example
```
export LATENTSPEED_HYPERLIQUID_USER_ADDRESS=0xabc123...
export LATENTSPEED_HYPERLIQUID_PRIVATE_KEY=0xdeadbeef...
export LATENTSPEED_HL_SIGNER_PYTHON=latentspeed/.venv-hl-signer/bin/python
export LATENTSPEED_HL_SIGNER_SCRIPT=latentspeed/tools/hl_signer_bridge.py
cd latentspeed/build/linux-release
./trading_engine_service --exchange hyperliquid --enable-market-data
```

## 7) Testnet vs mainnet
- The adapter uses `HyperliquidConfig::for_network(testnet)` internally.
- Testnet base URLs: `https://api.hyperliquid-testnet.xyz`, WS `wss://api.hyperliquid-testnet.xyz/ws`
- Mainnet base URLs: `https://api.hyperliquid.xyz`, WS `wss://api.hyperliquid.xyz/ws`

## 8) Common pitfalls to avoid
- Using the agent (API wallet) address for queries/streams. Always pass the actual user/subaccount address as `api_key`.
- Formatting numbers with trailing zeros. Provide strings; the adapter trims for signing.
- Invalid cloid format. If you include `client_order_id`, it must be `0x` + 32 hex chars.
- Not setting signer env vars when running from non-root paths; the default relative script may not resolve.

For deeper API details and latency tips, see `HYPERLIQUID_REFERENCE.md`.
