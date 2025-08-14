# Message Contract for Execution & Backtest

## Overview

* Strategies send an order **after** risk checks.
* The execution or backtest engine receives an **ExecutionOrder**.
* The engine replies with **ExecutionReport** (acks, rejections, cancels) and **Fill** (trades).
* All times are Unix **nanoseconds** unless stated otherwise.

---

## 1) ExecutionOrder (what you receive)

### Purpose

Tell the engine exactly what to do: place, cancel, or replace an order. Works for centralized exchanges (CEX) and decentralized trades (DEX), including AMM and CLMM swaps, and simple on-chain transfers.

### Top-level fields (always present)

```json
{
  "version": 1,
  "cl_id": "string",               // client order id (unique); idempotency key
  "action": "place|cancel|replace",
  "venue_type": "cex|dex|chain",   // centralized exchange, decentralized, or chain action
  "venue": "string",               // e.g., "bybit", "binance", "uniswap_v3", "jupiter", "pancakeswap_v3"
  "product_type": "spot|perpetual|amm_swap|clmm_swap|transfer",
  "details": { /* one of the variants below */ },
  "ts_ns": 0,                      // send time (ns)
  "tags": { "optional_name": "optional_value" }  // free-form metadata
}
```

> The **details** object changes by product\_type. Only include the fields for that variant.

---

### 1A) CEX spot/perpetual order (details)

For centralized exchanges via **ccxt** (spot or perpetual futures).

```json
{
  "symbol": "ETH/USDT",            // exchange symbol format (ccxt style)
  "side": "buy|sell",
  "order_type": "limit|market|stop|stop_limit",
  "time_in_force": "gtc|ioc|fok|post_only",
  "size": 0.01,                    // base asset amount; use exchange step sizes
  "price": 2500.0,                 // required for limit/stop_limit
  "stop_price": 2490.0,            // only for stop/stop_limit
  "reduce_only": false,            // true for reducing a perp position
  "margin_mode": "cross|isolated|null",
  "params": {                      // passthrough to ccxt params (optional)
    "clientOrderId": "my-tag-123",
    "leverage": 5
  }
}
```

**Plain English**

* **gtc** = “good till canceled” (stays open).
* **ioc** = “immediate or cancel” (fill what you can now).
* **fok** = “fill or kill” (all now or nothing).
* **post\_only** = make sure it adds liquidity (no taker fills).

**Mapping to ccxt**

* `createOrder(symbol, type, side, amount, price, params)`

  * `type` ⇐ `order_type`
  * `amount` ⇐ `size`
  * `price` ⇐ `price` (omit for market)
  * `params` ⇐ `params` (pass as-is)
* For stop/stop\_limit, many exchanges expect `params` like `stopPrice`; we already include `stop_price`—engine should map it into `params` if the exchange needs that.

---

### 1B) DEX AMM swap (details)

For “constant product” swaps (e.g., Uniswap v2, Sushiswap) via **Hummingbot Gateway**.

```json
{
  "chain": "ethereum",             // chain name the gateway expects
  "protocol": "uniswap_v2",        // or "sushiswap", etc.
  "token_in": "ETH",               // or full address if required by gateway
  "token_out": "USDC",
  "trade_mode": "exact_in|exact_out",
  "amount_in": 0.5,                // required if trade_mode=exact_in
  "amount_out": null,              // required if trade_mode=exact_out
  "slippage_bps": 50,              // 50 bps = 0.50% max slippage
  "deadline_sec": 120,             // seconds from now the tx stays valid
  "recipient": "0xYourWallet",
  "route": ["WETH", "USDC"],       // optional hop list; engine may leave to router
  "params": {                      // passthrough to gateway if needed
    "gasPrice": null, "gasLimit": null
  }
}
```

**Plain English**

* **exact\_in**: you fix how much you sell; engine must get **at least** min out.
* **exact\_out**: you fix how much you buy; engine must spend **at most** max in.
* Engine computes min/max with `slippage_bps` and sets the proper limits for the gateway.

---

### 1C) DEX CLMM swap (details)

For “concentrated liquidity” swaps (e.g., Uniswap v3, Pancake v3) via **Hummingbot Gateway**.

```json
{
  "chain": "ethereum",
  "protocol": "uniswap_v3",
  "pool": {
    "token0": "WETH",
    "token1": "USDC",
    "fee_tier_bps": 3000          // 0.30% pool; typical tiers: 500, 3000, 10000
  },
  "trade_mode": "exact_in|exact_out",
  "amount_in": 0.5,
  "amount_out": null,
  "slippage_bps": 50,
  "price_limit": null,            // optional limit; engine may map to sqrtPriceLimit
  "deadline_sec": 120,
  "recipient": "0xYourWallet",
  "params": { }
}
```

**Plain English**

* Same idea as AMM swaps, but **you must pick a pool** (which includes its fee).
* `price_limit` is optional protection against crossing too far; if not provided, engine can skip it.

---

### 1D) On-chain transfer (details)

Simple token or native coin transfer (useful for funding/ops) via **Hummingbot Gateway**.

```json
{
  "chain": "ethereum",
  "token": "USDC",                 // or native coin like "ETH"
  "amount": 1000.0,
  "to_address": "0xRecipient",
  "params": { "gasPrice": null, "gasLimit": null }
}
```

**Note:** You cannot “cancel” a chain transfer once sent. If `action=cancel` arrives for a transfer, engine should reject it.

---

### 1E) Cancel / Replace

* **Cancel** (CEX only):

  ```json
  {
    "cancel": {
      "symbol": "ETH/USDT",
      "cl_id_to_cancel": "same-as-original",   // prefer client id
      "exchange_order_id": null                // fallback if client id unknown
    }
  }
  ```

* **Replace** (CEX only; engine may implement as cancel+new):

  ```json
  {
    "replace": {
      "symbol": "ETH/USDT",
      "cl_id_to_replace": "original-clid",
      "new_price": 2499.0,
      "new_size": 0.02
    }
  }
  ```

**Note:** Swaps on DEX are atomic; no real cancel/replace once the transaction is sent.

---

## 2) ExecutionReport (what you publish back)

### Purpose

Tell the strategy/risk side what happened to the order (accepted, rejected, canceled, replaced). This is **not** a fill; see **Fill** below.

```json
{
  "version": 1,
  "cl_id": "string",
  "status": "accepted|rejected|canceled|replaced",
  "exchange_order_id": "string|null",
  "reason_code": "ok|invalid_params|risk_blocked|venue_reject|insufficient_balance|min_size|price_out_of_bounds|rate_limited|network_error|expired",
  "reason_text": "human readable explanation",
  "ts_ns": 0,
  "tags": { }
}
```

**Notes**

* For CEX, try to return the exchange’s order id when possible.
* For DEX swaps and chain transfers, set `exchange_order_id` to the transaction hash when you get it (publish a second report if it becomes known later).

---

## 3) Fill (what you publish back)

### Purpose

Report an actual trade execution (partial or full).

```json
{
  "version": 1,
  "cl_id": "string",
  "exchange_order_id": "string|null",
  "exec_id": "string",            // unique per fill; use venue’s exec id or hash
  "symbol_or_pair": "ETH/USDT",   // CEX symbol OR "ETH->USDC" for swaps
  "price": 2500.10,               // average price for this fill
  "size": 0.005,                  // base amount filled in this event
  "fee_currency": "USDT",
  "fee_amount": 0.05,
  "liquidity": "maker|taker|null",
  "ts_ns": 0,
  "tags": { }
}
```

**Notes**

* DEX swaps may produce a single “fill” event per transaction with the realized amounts and the effective price (out/in).
* For partial fills on CEX, publish multiple Fill messages for the same `cl_id`.

---

## 4) Transport (suggested)

Use your existing sockets (feel free to pick your own addresses):

* **Orders to execution/backtest:**
  PUSH → `tcp://127.0.0.1:5601` (topic-less; one message = one `ExecutionOrder`)

* **Engine outputs:**
  PUB → `tcp://127.0.0.1:5602` with topics:

  * `exec.report` → `ExecutionReport` (JSON)
  * `exec.fill` → `Fill` (JSON)

Engine must treat `cl_id` as **idempotent**: if it receives the same `ExecutionOrder` twice, it should not place it twice.

---

## 5) Examples

### A) CEX spot LIMIT, post-only, good-til-canceled

```json
{
  "version": 1,
  "cl_id": "b1a2c3...",
  "action": "place",
  "venue_type": "cex",
  "venue": "bybit",
  "product_type": "spot",
  "ts_ns": 1723360000000000000,
  "details": {
    "symbol": "ETH/USDT",
    "side": "buy",
    "order_type": "limit",
    "time_in_force": "post_only",
    "size": 0.02,
    "price": 2500.0,
    "reduce_only": false,
    "margin_mode": null,
    "params": { "clientOrderId": "mm-eth-001" }
  }
}
```

### B) CEX perpetual MARKET, immediate-or-cancel, reduce-only

```json
{
  "version": 1,
  "cl_id": "d4e5f6...",
  "action": "place",
  "venue_type": "cex",
  "venue": "bybit",
  "product_type": "perpetual",
  "ts_ns": 1723360005000000000,
  "details": {
    "symbol": "ETH/USDT:USDT",
    "side": "sell",
    "order_type": "market",
    "time_in_force": "ioc",
    "size": 0.03,
    "price": null,
    "reduce_only": true,
    "margin_mode": "cross",
    "params": { "clientOrderId": "hedge-eth-roc-01" }
  }
}
```

### C) DEX AMM swap: EXACT\_IN 0.5 ETH → USDC on Uniswap v2

```json
{
  "version": 1,
  "cl_id": "swap-eth-usdc-01",
  "action": "place",
  "venue_type": "dex",
  "venue": "uniswap_v2",
  "product_type": "amm_swap",
  "ts_ns": 1723360010000000000,
  "details": {
    "chain": "ethereum",
    "protocol": "uniswap_v2",
    "token_in": "ETH",
    "token_out": "USDC",
    "trade_mode": "exact_in",
    "amount_in": 0.5,
    "amount_out": null,
    "slippage_bps": 50,
    "deadline_sec": 120,
    "recipient": "0xYourWallet",
    "route": ["WETH", "USDC"],
    "params": {}
  }
}
```

### D) DEX CLMM swap: EXACT\_OUT 1000 USDC → ETH on Uniswap v3 (0.30% fee)

```json
{
  "version": 1,
  "cl_id": "clmm-usdc-eth-01",
  "action": "place",
  "venue_type": "dex",
  "venue": "uniswap_v3",
  "product_type": "clmm_swap",
  "ts_ns": 1723360015000000000,
  "details": {
    "chain": "ethereum",
    "protocol": "uniswap_v3",
    "pool": { "token0": "WETH", "token1": "USDC", "fee_tier_bps": 3000 },
    "trade_mode": "exact_out",
    "amount_in": null,
    "amount_out": 1000.0,
    "slippage_bps": 50,
    "price_limit": null,
    "deadline_sec": 120,
    "recipient": "0xYourWallet",
    "params": {}
  }
}
```

### E) Cancel a CEX order by client id

```json
{
  "version": 1,
  "cl_id": "cancel-123",
  "action": "cancel",
  "venue_type": "cex",
  "venue": "bybit",
  "product_type": "spot",
  "ts_ns": 1723360020000000000,
  "details": {
    "symbol": "ETH/USDT",
    "side": null,
    "order_type": null,
    "time_in_force": null,
    "size": null,
    "price": null,
    "params": {},
    "cancel": { "cl_id_to_cancel": "mm-eth-001", "exchange_order_id": null }
  }
}
```

---

## 6) Backtest rules (same messages)

* Backtest consumes **ExecutionOrder** messages and must publish **ExecutionReport** and **Fill** in the exact same shape.
* Deterministic: given the same input stream and seeds, the same orders get the same reports/fills.

---

## 7) Field rules and edge cases

* **Numbers and precision:** Use native floats for simplicity; the engine should round to each venue’s step sizes before sending.
* **Symbols and tokens:** For CEX, use the exchange’s symbol format that ccxt expects. For DEX, if the gateway needs addresses, accept symbols and let the engine map them, or pass full addresses in `params`.
* **Idempotency:** If the engine sees the same `cl_id` again with `action=place`, it must ignore the duplicate or return the same `ExecutionReport`.
* **Unsupported actions:** If a cancel/replace is not supported for the product (e.g., DEX swaps), reply with `ExecutionReport.status="rejected"` and `reason_code="invalid_params"`.

---

## 8) Quick mapping guide

### To ccxt `createOrder`

* `symbol` ⇐ `details.symbol`
* `type` ⇐ `details.order_type`
* `side` ⇐ `details.side`
* `amount` ⇐ `details.size`
* `price` ⇐ `details.price` (omit for market)
* `params` ⇐ `details.params` + map `stop_price` as needed
* `time_in_force` and `post_only` usually go inside `params` per exchange rules

### To Hummingbot Gateway (AMM/CLMM)

* Endpoint usually needs: `chain`, `connector`/`protocol`, tokens, amounts, `slippage`, `deadline`, and optional `route` or `pool`.
* From `details`: pass fields as-is; compute min/max amounts using `slippage_bps`.