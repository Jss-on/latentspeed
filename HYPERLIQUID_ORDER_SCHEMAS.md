# Hyperliquid Perpetuals: Order & Report Schemas

## Overview

Hyperliquid is a **decentralized perpetuals exchange** on a custom L1 blockchain with:
- **Wallet-based authentication** (not API keys)
- **Integer asset indices** instead of symbol strings  
- **L1 EIP-712 signing** via Python SDK bridge
- **WebSocket streams** for real-time order updates and fills
- **Venue classification**: Treated as `venue_type="cex"` with `product_type="perpetual"` (not DEX AMM/CLMM)

---

## 1. Outbound: Order Placement (to Hyperliquid API)

### A. HTTP/WS Order Request (POST `/exchange`)

**Hyperliquid Native Format:**

```json
{
  "action": {
    "type": "order",
    "grouping": "na",
    "orders": [
      {
        "a": 0,                  // asset index (int): BTC=0, ETH=1, etc. from /info meta
        "b": true,               // isBuy (bool): true=buy, false=sell
        "p": "65000",            // limit price (string): trim trailing zeros, 5 sig figs max
        "s": "0.01",             // size (string): rounded to szDecimals (e.g., 3 for BTC)
        "r": false,              // reduceOnly (bool): true = close-only order
        "t": {                   // order type
          "limit": {
            "tif": "Gtc"         // "Gtc" | "Ioc" | "Alo" (post-only)
          }
        },
        "c": "0x1234...abcdef"   // cloid (string): optional, must be 0x + 32 hex chars
      }
    ]
  },
  "nonce": 1730160000123,        // monotonic ms timestamp (unique per signer)
  "signature": {                 // EIP-712 signature from agent wallet
    "r": "0x...",
    "s": "0x...",
    "v": 27
  },
  "vaultAddress": null           // optional: subaccount/vault address
}
```

**Field Details:**

| Field | Type | Description | Example |
|-------|------|-------------|---------|
| `a` | int | Asset index from `/info` metadata (perpetuals) | `0` (BTC), `1` (ETH) |
| `b` | bool | Buy side flag | `true` = buy, `false` = sell |
| `p` | string | Limit price (decimal string, trim trailing zeros) | `"65000"`, `"3395.5"` |
| `s` | string | Order size in base units (rounded to `szDecimals`) | `"0.01"` (BTC), `"0.75"` (ETH) |
| `r` | bool | Reduce-only flag | `false` = open new, `true` = close existing |
| `t` | object | Order type with TIF | See TIF mapping below |
| `c` | string | Client order ID (optional) | `"0x" + 32 hex chars` |

**Time-in-Force (TIF) Mapping:**

| Engine TIF | Hyperliquid Format | Behavior |
|------------|-------------------|----------|
| `GTC` | `{"limit": {"tif": "Gtc"}}` | Good-til-canceled (resting limit) |
| `IOC` | `{"limit": {"tif": "Ioc"}}` | Immediate-or-cancel (market-like) |
| `PO` / `Alo` | `{"limit": {"tif": "Alo"}}` | Add-liquidity-only (post-only) |

**Market Orders:**  
Hyperliquid doesn't have pure market orders. Use `{"limit": {"tif": "Ioc"}}` with aggressive pricing.

---

### B. ExecutionOrder Schema (Python trading_core → C++ engine on ZMQ 5601)

When sending from **trading_core** to the **C++ engine**, use this schema:

```json
{
  "version": 1,
  "cl_id": "leadlag-btc-001",
  "action": "place",
  "venue_type": "cex",               // Hyperliquid uses CEX semantics, not DEX
  "venue": "hyperliquid",
  "product_type": "perpetual",       // NOT "spot"
  "details": {
    "symbol": "BTC/USDT:USDT",       // CCXT-style perp symbol (auto-resolves to asset index)
    "side": "buy",                   // "buy" | "sell"
    "order_type": "limit",           // "limit" | "market" (mapped to IOC internally)
    "time_in_force": "gtc",          // "gtc" | "ioc" | "post_only"
    "size": 0.01,                    // decimal (engine converts to string)
    "price": 65000.0,                // decimal (engine formats to wire string)
    "reduce_only": false,
    "margin_mode": "cross",          // "cross" | "isolated" (Hyperliquid uses cross by default)
    "params": {}
  },
  "ts_ns": 1730160000000000000,
  "tags": {"strategy": "AdaptiveLeadLag"}
}
```

**Key Differences from Standard CEX:**
- **Symbol resolution**: Engine resolves `"BTC/USDT:USDT"` → asset index `0` via cached `/info` metadata
- **No exchange_order_id** until response received
- **Client order ID** auto-generated if not provided (must be 0x + 32 hex if custom)

---

### C. Cancel Request

**Hyperliquid Native:**

```json
{
  "action": {
    "type": "cancel",
    "cancels": [
      {
        "a": 0,                       // asset index
        "o": 123456789                // exchange order ID (oid) as int64
      }
    ]
  },
  "nonce": 1730160001234,
  "signature": { "r": "0x...", "s": "0x...", "v": 27 }
}
```

**Via ExecutionOrder:**

```json
{
  "version": 1,
  "cl_id": "cancel-001",
  "action": "cancel",
  "venue_type": "cex",
  "venue": "hyperliquid",
  "product_type": "perpetual",
  "details": {
    "symbol": "BTC/USDT:USDT",
    "cancel": {
      "cl_id_to_cancel": "leadlag-btc-001",
      "exchange_order_id": "123456789"   // optional if cloid available
    }
  }
}
```

---

## 2. Inbound: Execution Reports & Fills (Hyperliquid → Engine on ZMQ 5602)

### A. Order Response (HTTP/WS `/exchange` response)

**Success Response:**

```json
{
  "status": "ok",
  "response": {
    "type": "order",
    "data": {
      "statuses": [
        {
          "resting": {
            "oid": 123456789           // exchange order ID (int64)
          }
        }
      ]
    }
  }
}
```

**Immediate Fill:**

```json
{
  "status": "ok",
  "response": {
    "type": "order",
    "data": {
      "statuses": [
        {
          "filled": {
            "oid": 123456790,
            "totalSz": "0.01",         // filled size
            "avgPx": "64995.5"         // average fill price
          }
        }
      ]
    }
  }
}
```

**Error Response:**

```json
{
  "status": "err",
  "response": "Tick"                   // or other error code
}
```

**Common Error Codes:**

| Code | Meaning | Canonical Mapping |
|------|---------|-------------------|
| `Tick` | Price doesn't match tick size | `price_out_of_bounds` |
| `MinTradeNtl` | Below minimum notional | `min_size` |
| `PerpMargin` | Insufficient margin | `insufficient_balance` |
| `ReduceOnly` | Reduce-only violation | `risk_blocked` |
| `BadAloPx` | Post-only would cross | `post_only_violation` |
| `IocCancel` | IOC no immediate fill | `ok` (normal for IOC) |
| `MarketOrderNoLiquidity` | No liquidity | `venue_reject` |

---

### B. WebSocket User Stream (Subscription Channel)

**Subscribe Message:**

```json
{
  "method": "subscribe",
  "subscription": {
    "type": "user",
    "user": "0xabc123..."             // user wallet address (lowercase)
  }
}
```

**Order Update Message:**

```json
{
  "channel": "user",
  "data": {
    "orders": [
      {
        "oid": 123456789,             // exchange order ID
        "coin": "BTC",                // perpetual coin name
        "side": "B",                  // "B" = buy, "A" = sell (ask)
        "limitPx": "65000",           // limit price
        "sz": "0.01",                 // size
        "timestamp": 1730160000123,   // ms timestamp
        "cloid": "0x1234...abcdef",   // client order ID
        "order": {
          "status": "open",           // "open" | "filled" | "canceled" | "triggered" | "rejected"
          "filledSz": "0",            // filled size so far
          "origSz": "0.01"            // original size
        }
      }
    ]
  }
}
```

**Fill Message:**

```json
{
  "channel": "user",
  "data": {
    "fills": [
      {
        "tid": 987654321,             // trade ID
        "oid": 123456789,             // order ID
        "px": "64995.5",              // fill price
        "sz": "0.005",                // fill size
        "side": "B",                  // "B" = buy, "A" = sell
        "fee": "0.325",               // fee in USDC
        "time": 1730160001234,        // ms timestamp
        "cloid": "0x1234...abcdef"    // client order ID
      }
    ]
  }
}
```

**Funding Update:**

```json
{
  "channel": "user",
  "data": {
    "funding": [
      {
        "coin": "BTC",
        "fundingRate": "0.00001",     // funding rate
        "szi": "0.01",                // position size
        "usdc": "-0.65",              // funding payment (negative = paid)
        "time": 1730160000000
      }
    ]
  }
}
```

---

### C. ExecutionReport Schema (Engine → trading_core on ZMQ 5602)

**Accepted:**

```json
{
  "version": 1,
  "cl_id": "leadlag-btc-001",
  "status": "accepted",
  "exchange_order_id": "123456789",
  "reason_code": "ok",
  "reason_text": null,
  "ts_ns": 1730160001000000000,
  "tags": {}
}
```

**Rejected:**

```json
{
  "version": 1,
  "cl_id": "leadlag-btc-002",
  "status": "rejected",
  "exchange_order_id": null,
  "reason_code": "price_out_of_bounds",
  "reason_text": "Tick: price must align with tick size",
  "ts_ns": 1730160002000000000,
  "tags": {}
}
```

---

### D. Fill Schema (Engine → trading_core on ZMQ 5602)

```json
{
  "version": 1,
  "cl_id": "leadlag-btc-001",
  "exchange_order_id": "123456789",
  "exec_id": "987654321",
  "symbol_or_pair": "BTC/USDT:USDT",
  "side": "buy",
  "price": 64995.5,
  "size": 0.005,
  "fee_currency": "USDC",
  "fee_amount": 0.325,
  "liquidity": "taker",              // "maker" | "taker"
  "ts_ns": 1730160002000000000,
  "tags": {}
}
```

---

## 3. Key Implementation Details

### Asset Resolution

The engine maintains a cached mapping from symbol → asset index:

```cpp
// From /info metadata
{
  "BTC": 0,
  "ETH": 1,
  "SOL": 2,
  // ... etc
}
```

**Symbol formats:**
- Input: `"BTC"` or `"BTC/USDT:USDT"` (CCXT-style)
- Output: `0` (asset index sent to Hyperliquid)

### Number Formatting

**Critical**: Hyperliquid rejects numbers with trailing zeros in signatures.

```cpp
// ✅ CORRECT
"65000"      // no trailing .0
"0.01"       // trimmed
"3395.5"     // only needed decimals

// ❌ WRONG
"65000.0"    // trailing zero causes signature mismatch
"0.010"      // trimmed to 0.01
```

**Precision rules:**
- Prices: ≤5 significant figures, ≤(6 - szDecimals) decimal places
- Sizes: Must round to asset's `szDecimals` (BTC=3, ETH=4, SOL=2)

### Nonce Management

```cpp
// Monotonic millisecond timestamp per signer
uint64_t nonce = current_timestamp_ms();
// Must be unique, within (T-2days, T+1day) window
```

### Client Order ID Format

```cpp
// Optional but recommended for order tracking
// Must be exactly: "0x" + 32 hex characters
std::string cloid = "0x1234567890abcdef1234567890abcdef";
```

---

## 4. Status Mapping (Hyperliquid → trading_core)

| Hyperliquid Status | trading_core Status | ExecutionReport.status |
|--------------------|---------------------|----------------------|
| `resting` (partial) | `new` | `accepted` |
| `open` | `accepted` / `partially_filled` | `accepted` |
| `filled` | `filled` | `accepted` (then Fill event) |
| `canceled` | `canceled` | `canceled` |
| `rejected` | `rejected` | `rejected` |
| `triggered` | `triggered` | `accepted` |
| `tickRejected` | `rejected` | `rejected` (reason: `price_out_of_bounds`) |
| `marginCanceled` | `canceled` | `canceled` (reason: `insufficient_balance`) |

---

## 5. Complete Example Flow

### Step 1: Strategy sends ProposedOrder to OrderManager

```python
# In AdaptiveLeadLagStrategy
order = ProposedOrder(
    cl_id="leadlag-btc-001",
    strategy="AdaptiveLeadLag",
    symbol="BTC-USDT",              # normalized symbol
    side=Side.buy,
    type=OrderType.limit,
    tif=TIF.ioc,
    sz=0.01,
    px=65000.0,
    reduce_only=False,
    venue_type=VenueType.cex,       # Hyperliquid = CEX semantics
    product_type=ProductType.perpetual,
    meta={"venue": "hyperliquid"}
)
await ctx.submit_order(order)
```

### Step 2: OrderManager validates via RiskEngine, converts to ExecutionOrder

```python
# OrderManager → ExecClient
execution_order = build_exec_order(order)
# Sends via ZMQ PUSH to port 5601
```

### Step 3: C++ Engine receives and processes

```cpp
// HyperliquidAdapter::place_order
// 1. Resolve "BTC-USDT" → asset index 0
// 2. Format price/size strings (trim zeros)
// 3. Generate/use cloid
// 4. Call Python signer bridge for EIP-712 signature
// 5. POST to /exchange or WS post
```

### Step 4: Hyperliquid responds

```json
{
  "status": "ok",
  "response": {
    "data": {
      "statuses": [{
        "resting": {"oid": 123456789}
      }]
    }
  }
}
```

### Step 5: Engine publishes ExecutionReport

```json
{
  "version": 1,
  "cl_id": "leadlag-btc-001",
  "status": "accepted",
  "exchange_order_id": "123456789",
  "reason_code": "ok"
}
```

### Step 6: WebSocket user stream sends fill

```json
{
  "channel": "user",
  "data": {
    "fills": [{
      "tid": 987654,
      "oid": 123456789,
      "px": "64998",
      "sz": "0.01",
      "side": "B",
      "fee": "0.65",
      "cloid": "0x..."
    }]
  }
}
```

### Step 7: Engine publishes Fill

```json
{
  "version": 1,
  "cl_id": "leadlag-btc-001",
  "exchange_order_id": "123456789",
  "exec_id": "987654",
  "symbol_or_pair": "BTC/USDT:USDT",
  "side": "buy",
  "price": 64998.0,
  "size": 0.01,
  "fee_currency": "USDC",
  "fee_amount": 0.65,
  "liquidity": "taker"
}
```

---

## 6. References

- **Hyperliquid API Docs**: https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api
- **Python SDK** (canonical signing): https://github.com/hyperliquid-dex/hyperliquid-python-sdk
- **Internal Docs**:
  - `docs/HYPERLIQUID_REFERENCE.md` - Complete API reference
  - `docs/HYPERLIQUID_ADAPTER_USAGE.md` - Adapter usage guide
  - `QUICKSTART_HYPERLIQUID.md` - Quick start guide

---

## 7. Common Pitfalls

1. **Using DEX schemas**: Hyperliquid uses CEX patterns (`venue_type="cex"`, `product_type="perpetual"`), not AMM/CLMM
2. **Trailing zeros**: Always trim `"65000.0"` → `"65000"` before signing
3. **Asset mapping**: Must cache `/info` metadata to resolve symbols → asset indices
4. **Client order ID format**: Must be exactly `"0x" + 32 hex chars` if provided
5. **Nonce uniqueness**: Use monotonic ms timestamp per signer process
6. **Agent vs user address**: Query account state with user address, sign with agent wallet
7. **IOC cancels**: `IocCancel` is normal behavior, not an error (map to `ok`)
8. **Size decimals**: BTC=3, ETH=4, SOL=2 — violating this causes `Tick` errors

---

**End of Schema Documentation**
