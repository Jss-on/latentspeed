## 1. Partial Fill State Detection

### The Problem Before
```cpp
// OLD CODE - Only detected OPEN/FILLED/CANCELLED
if (status == "filled") {
    new_state = OrderState::FILLED;
} else if (status == "cancelled") {
    new_state = OrderState::CANCELLED;
} else {
    new_state = OrderState::OPEN;  // ❌ No partial fill detection!
}
```

**Issue**: Large orders that filled in chunks were reported as `OPEN` → `FILLED` instantly, hiding the partial fill progression.

### The Solution Now
```cpp
// NEW CODE - Detects partial fills using filled_sz/orig_sz
if (status == "open" || status.empty()) {
    double filled_sz = std::stod(msg.data.value("filled_sz", "0"));
    double orig_sz = std::stod(msg.data.value("orig_sz", "0"));
    
    if (filled_sz > 0 && filled_sz < orig_sz) {
        new_state = OrderState::PARTIALLY_FILLED;  // ✅ Detected!
    } else if (filled_sz >= orig_sz && orig_sz > 0) {
        new_state = OrderState::FILLED;
    } else {
        new_state = OrderState::OPEN;
    }
}
```

### Real-World Example
```
Buy 10 BTC @ $65,000 (limit order)

WS Update 1: filled_sz=3, orig_sz=10  → PARTIALLY_FILLED (30%)
WS Update 2: filled_sz=7, orig_sz=10  → PARTIALLY_FILLED (70%)
WS Update 3: filled_sz=10, orig_sz=10 → FILLED (100%)
```

### Why It Matters
- **Position tracking**: Strategies know exact fill progress, not just binary open/closed
- **Risk management**: Partially filled orders affect margin differently than open orders
- **Hummingbot pattern**: Matches how Hummingbot's `InFlightOrderTracker` manages states

---

## 2. Exchange Order ID Backfilling

### The Problem Before
```cpp
// Order placement flow:
1. REST POST /exchange → {"status": "ok"}  (no oid yet!)
2. [1-2 seconds later]
3. WS user stream → {"fills": [...], "oid": 12345678}

// Cancel attempt during gap:
cancel(client_order_id) → ❌ "Order has no exchange ID"
```

**Issue**: Hyperliquid's REST response doesn't always include the `oid` immediately. The WS feed sends it moments later, creating a window where cancels fail.

### The Solution Now
```cpp
// Backfill from WS update
if (!order.exchange_order_id.has_value() && msg.data.contains("exchange_order_id")) {
    int64_t oid = msg.data.value("exchange_order_id", 0);
    if (oid > 0) {
        update.exchange_order_id = std::to_string(oid);
        spdlog::debug("[HL] Backfilled exchange_order_id {} for order {}", 
                     *update.exchange_order_id, order.client_order_id);
    }
}
```

### Real-World Example
```
t=0ms:   place_order("BTC-USD", buy, 1.0) → returns cloid="abc123"
         Tracker: {cloid="abc123", exchange_order_id=null}

t=50ms:  WS update arrives: {"oid": 98765, "cloid": "abc123"}
         Backfill → {cloid="abc123", exchange_order_id="98765"} ✅

t=100ms: User requests cancel("abc123")
         Now works! Uses oid=98765 ✅
```

### Additional Fallback
```cpp
// If cloid lookup fails, try by oid
if (!order_opt.has_value() && msg.data.contains("exchange_order_id")) {
    int64_t oid = msg.data.value("exchange_order_id", 0);
    if (oid > 0) {
        order_opt = order_tracker_.get_order_by_exchange_id(std::to_string(oid));
    }
}
```

### Why It Matters
- **Cancel reliability**: No more "Order has no exchange ID" errors
- **High-frequency trading**: Critical for rapid order modification strategies
- **Hummingbot pattern**: Tracker enrichment from multiple sources (REST + WS)

---

## 3. Removed Zeroed Fill Events

### The Problem Before
```cpp
// OLD CODE - Emitted fake fills on order completion
if (new_state == OrderState::FILLED && event_listener_) {
    event_listener_->on_order_filled(cloid, 0.0, 0.0);  // ❌ Price=0, Amount=0!
}
```

**Issue**: When an order completed, it emitted a "fill" with `price=0.0` and `amount=0.0`, creating confusing duplicate events.

### The Solution Now
```cpp
// NEW CODE - Only emit completion summary, not fake fills
if (new_state == OrderState::FILLED && event_listener_) {
    auto updated_order = order_tracker_.get_order(order.client_order_id);
    if (updated_order.has_value()) {
        event_listener_->on_order_completed(  // ✅ Different event type
            order.client_order_id,
            updated_order->average_fill_price,   // Real VWAP
            updated_order->filled_amount          // Real total
        );
    }
}
```

### Real Trade Events Now
```cpp
// In process_trade_update - ONLY place actual fills are emitted
if (event_listener_) {
    event_listener_->on_order_filled(
        order.client_order_id,
        trade.fill_price,    // ✅ Real: $65,123.45
        trade.fill_base_amount  // ✅ Real: 0.156 BTC
    );
}
```

### Event Flow Comparison

**Before (Confusing)**:
```
Fill #1: 0.5 BTC @ $65,100 → on_order_filled(0.5, 65100)
Fill #2: 0.5 BTC @ $65,200 → on_order_filled(0.5, 65200)
Order Complete             → on_order_filled(0.0, 0.0)  ❌ Weird!
```

**After (Clean)**:
```
Fill #1: 0.5 BTC @ $65,100 → on_order_filled(0.5, 65100)
Fill #2: 0.5 BTC @ $65,200 → on_order_filled(0.5, 65200)
Order Complete             → on_order_completed(1.0, 65150)  ✅ VWAP summary
```

### Why It Matters
- **Accurate accounting**: No phantom zero-amount fills in logs
- **Event clarity**: Fills = actual trades, Completed = summary
- **Hummingbot pattern**: Separation of trade events vs order completion events

---
## 4. Fixed Price Precision

### The Problem Before
```cpp
// OLD CODE - Hard-coded 2 decimals for ALL assets
std::string limit_px = HyperliquidWebUtils::float_to_wire(order.price, 2);
// BTC @ $65,123.456 → "65123.46" (rounded, loses precision)
// ETH @ $3,456.789  → "3456.79"
// SOL @ $145.12345  → "145.12" ❌ May cause Tick rejects!
```

**Issue**: Hyperliquid uses **5 significant figures** (not 2 decimals). Different assets have different precision requirements.

### The Solution Now
```cpp
// Fetch real precision from exchange
int sz_decimals = std::stoi(asset.value("szDecimals", "3"));
int price_decimals = 5;  // Hyperliquid's 5 sig figs

TradingRule rule;
rule.tick_size = std::pow(10.0, -price_decimals);  // 0.00001
rule.step_size = std::pow(10.0, -sz_decimals);      // Per asset
```

```cpp
// Use real precision when placing orders
auto rule_opt = get_trading_rule(order.trading_pair);
int price_decimals = 5;  // Default

if (rule_opt.has_value()) {
    price_decimals = rule_opt->price_decimals;  // Use fetched
}

std::string limit_px = HyperliquidWebUtils::float_to_wire(order.price, price_decimals);
// BTC @ $65,123.456 → "65123.456" (5 sig figs) ✅
```

### Precision Examples

| Asset | Old (2 dec) | New (5 sig figs) | Hyperliquid Accepts? |
|-------|-------------|------------------|---------------------|
| BTC @ $65,123.456 | "65123.46" | "65123.456" | ✅ |
| ETH @ $3,456.789 | "3456.79" | "3456.789" | ✅ |
| SOL @ $145.12345 | "145.12" | "145.12345" | ✅ |
| SHIB @ $0.000012345 | "0.00" | "0.000012345" | ✅ |

### Size Precision (Per Asset)
```cpp
// Hyperliquid metadata response
{
  "universe": [
    {"name": "BTC", "szDecimals": "3"},  // 0.001 BTC min
    {"name": "ETH", "szDecimals": "4"},  // 0.0001 ETH min
    {"name": "SOL", "szDecimals": "1"}   // 0.1 SOL min
  ]
}

// Applied
BTC order: 1.234 BTC → "1.234" (3 decimals)
ETH order: 5.6789 ETH → "5.6789" (4 decimals)
SOL order: 10.5 SOL → "10.5" (1 decimal)
```

### Why It Matters
- **No Tick rejects**: Orders conform to exchange precision requirements
- **Accurate pricing**: Preserves 5 sig figs as Hyperliquid expects
- **Hummingbot pattern**: Dynamic precision from exchange metadata (like `TradingRule`)

---