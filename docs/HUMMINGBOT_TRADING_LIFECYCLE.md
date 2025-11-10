# Hummingbot Trading Lifecycle: Data → Strategy → Orders

## Overview
This document explains the complete lifecycle of trading in Hummingbot, from market data ingestion to strategy execution to order placement.

## Architecture Components

```
┌─────────────────────────────────────────────────────────────────┐
│                         TradingCore                              │
│  - ConnectorManager (manages all exchanges)                      │
│  - Strategy (optional - can run without)                         │
│  - Clock (time-based event orchestrator)                         │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                         Clock Mechanism                          │
│  - Tick size: 1.0s by default                                   │
│  - Real-time mode: asyncio.sleep until next tick                │
│  - Backtest mode: simulated time progression                    │
│  - TimeIterator children: connectors, strategies                │
└─────────────────────────────────────────────────────────────────┘
                              │
                ┌─────────────┴─────────────┐
                ▼                           ▼
┌──────────────────────────┐   ┌──────────────────────────┐
│   ExchangePyBase         │   │   StrategyBase           │
│   (ConnectorBase)        │   │   (TimeIterator)         │
└──────────────────────────┘   └──────────────────────────┘
```

---

## Part 1: Market Data Ingestion

### 1.1 Exchange Connector Initialization

**File:** `hummingbot/connector/exchange_py_base.py`

```python
class ExchangePyBase(ExchangeBase, ABC):
    def __init__(self):
        # Order Book Tracking
        self._orderbook_ds = self._create_order_book_data_source()
        self._set_order_book_tracker(OrderBookTracker(...))
        
        # User Stream (account updates)
        self._user_stream_tracker = self._create_user_stream_tracker()
        
        # Order Management
        self._order_tracker = self._create_order_tracker()
        
        # Network Health
        self._time_synchronizer = TimeSynchronizer()
        self._throttler = AsyncThrottler(rate_limits=self.rate_limits_rules)
```

### 1.2 Order Book Data Source

**File:** `hummingbot/core/data_type/order_book_tracker.py`

The OrderBookTracker spawns multiple async tasks:

```python
def start(self):
    # 1. Listen for WebSocket order book diffs
    self._order_book_diff_listener_task = safe_ensure_future(
        self._data_source.listen_for_order_book_diffs(
            self._ev_loop, self._order_book_diff_stream
        )
    )
    
    # 2. Listen for WebSocket trades
    self._order_book_trade_listener_task = safe_ensure_future(
        self._data_source.listen_for_trades(
            self._ev_loop, self._order_book_trade_stream
        )
    )
    
    # 3. Listen for WebSocket order book snapshots
    self._order_book_snapshot_listener_task = safe_ensure_future(
        self._data_source.listen_for_order_book_snapshots(
            self._ev_loop, self._order_book_snapshot_stream
        )
    )
    
    # 4. Route messages to appropriate order books
    self._order_book_diff_router_task = safe_ensure_future(
        self._order_book_diff_router()
    )
    
    # 5. Update last trade prices
    self._update_last_trade_prices_task = safe_ensure_future(
        self._update_last_trade_prices_loop()
    )
```

**Key Data Structures:**
- `_order_books: Dict[str, OrderBook]` - Maintains live order books per trading pair
- `_order_book_diff_stream: asyncio.Queue` - Stream of incremental updates
- `_order_book_snapshot_stream: asyncio.Queue` - Stream of full snapshots
- `_order_book_trade_stream: asyncio.Queue` - Stream of trade executions

### 1.3 User Stream Data Source

**Purpose:** Receives account-level updates via WebSocket

**Events Tracked:**
- Order fills
- Order cancellations
- Balance updates
- Position updates (derivatives)

---

## Part 2: Clock-Based Event System

### 2.1 Clock Implementation

**File:** `hummingbot/core/clock.pyx`

```cython
async def run_til(self, timestamp: float):
    # Start all child iterators (connectors, strategies)
    for child_iterator in self._current_context:
        child_iterator.c_start(self, self._current_tick)
    
    # Main loop
    while True:
        # Sleep until next tick
        next_tick_time = ((now // self._tick_size) + 1) * self._tick_size
        await asyncio.sleep(next_tick_time - now)
        self._current_tick = next_tick_time
        
        # Tick all children (THIS IS WHERE MAGIC HAPPENS)
        for child_iterator in self._current_context:
            try:
                child_iterator.c_tick(self._current_tick)
            except Exception:
                self.logger().error("Unexpected error running clock tick.")
```

**Clock Children:**
1. **Connectors (ExchangeBase)** - Poll status, update balances
2. **Strategies (StrategyBase)** - Execute trading logic
3. **OrderTracker** - Track order states

### 2.2 NetworkIterator Pattern

**File:** `hummingbot/core/network_iterator.pyx`

All connectors inherit from `NetworkIterator`:

```cython
cdef c_start(self, Clock clock, double timestamp):
    # Start network health check loop
    self._check_network_task = safe_ensure_future(self._check_network_loop())
    self._network_status = NetworkStatus.NOT_CONNECTED
    
async def _check_network_loop(self):
    while True:
        new_status = await self.check_network()
        if new_status != last_status:
            if new_status is NetworkStatus.CONNECTED:
                await self.start_network()  # Start WebSocket streams
            else:
                await self.stop_network()   # Stop streams
        await asyncio.sleep(self._check_network_interval)
```

---

## Part 3: Strategy Execution

### 3.1 Strategy Initialization

**File:** `hummingbot/strategy/strategy_base.pyx`

```cython
cdef class StrategyBase(TimeIterator):
    def __init__(self):
        # Event listeners for market events
        self._sb_create_buy_order_listener = BuyOrderCreatedListener(self)
        self._sb_fill_order_listener = OrderFilledListener(self)
        self._sb_cancel_order_listener = OrderCancelledListener(self)
        # ... more listeners
        
        # Order tracker
        self._sb_order_tracker = OrderTracker()

cdef c_add_markets(self, list markets):
    # Register listeners to market events
    for market in markets:
        market.c_add_listener(self.ORDER_FILLED_EVENT_TAG, self._sb_fill_order_listener)
        market.c_add_listener(self.ORDER_CANCELED_EVENT_TAG, self._sb_cancel_order_listener)
        # ... register all event types
        self._sb_markets.add(market)
```

### 3.2 Strategy Tick (Pure Market Making Example)

**File:** `hummingbot/strategy/pure_market_making/pure_market_making.pyx`

```cython
cdef c_tick(self, double timestamp):
    # 1. Check if markets are ready (order books initialized)
    if not self._all_markets_ready:
        self._all_markets_ready = all([market.ready for market in self._sb_markets])
        if not self._all_markets_ready:
            return  # Wait for data
    
    # 2. Create order proposals
    proposal = None
    if self._create_timestamp <= self._current_timestamp:
        # a. Create base proposals (buy/sell orders)
        proposal = self.c_create_base_proposal()
        
        # b. Apply order level modifiers (order count limits)
        self.c_apply_order_levels_modifiers(proposal)
        
        # c. Apply price modifiers (spread, inventory skew)
        self.c_apply_order_price_modifiers(proposal)
        
        # d. Apply size modifiers
        self.c_apply_order_size_modifiers(proposal)
        
        # e. Apply budget constraint (available balances)
        self.c_apply_budget_constraint(proposal)
        
        # f. Filter out orders that would take liquidity
        if not self._take_if_crossed:
            self.c_filter_out_takers(proposal)
    
    # 3. Manage existing orders
    self._hanging_orders_tracker.process_tick()
    self.c_cancel_active_orders_on_max_age_limit()
    self.c_cancel_active_orders(proposal)
    
    # 4. Execute new orders if conditions met
    if self.c_to_create_orders(proposal):
        self.c_execute_orders_proposal(proposal)
```

### 3.3 Creating Order Proposals

```cython
cdef object c_create_base_proposal(self):
    market = self._market_info.market
    buys = []
    sells = []
    
    # Get reference price (mid_price, last_price, etc.)
    buy_reference_price = sell_reference_price = self.get_price()
    
    # Calculate bid orders
    for level in range(0, self._buy_levels):
        price = buy_reference_price * (Decimal("1") - self._bid_spread - (level * self._order_level_spread))
        price = market.c_quantize_order_price(self.trading_pair, price)
        size = self._order_amount + (self._order_level_amount * level)
        size = market.c_quantize_order_amount(self.trading_pair, size)
        if size > 0:
            buys.append(PriceSize(price, size))
    
    # Calculate ask orders (similar logic)
    # ...
    
    return Proposal(buys, sells)
```

### 3.4 Accessing Market Data in Strategy

```cython
# Get best bid/ask prices
bid_price = market.get_price(trading_pair, False)  # False = bid
ask_price = market.get_price(trading_pair, True)   # True = ask
mid_price = (bid_price + ask_price) / 2

# Get account balances
base_balance = market.get_balance(base_asset)
quote_balance = market.get_balance(quote_asset)
available_base = market.get_available_balance(base_asset)

# Get order book
order_book = market.get_order_book(trading_pair)
bids_df = order_book.snapshot[0]  # Pandas DataFrame
asks_df = order_book.snapshot[1]
```

---

## Part 4: Order Execution

### 4.1 Strategy to Connector Bridge

**File:** `hummingbot/strategy/strategy_base.pyx`

```python
def buy(self,
        connector_name: str,
        trading_pair: str,
        amount: Decimal,
        order_type: OrderType,
        price: Decimal,
        **kwargs) -> str:
    """
    Called by strategy to place a buy order.
    Returns client_order_id.
    """
    market = self._sb_markets_by_name[connector_name]
    return market.buy(trading_pair, amount, order_type, price, **kwargs)
```

### 4.2 Exchange Connector Order Placement

**File:** `hummingbot/connector/exchange/bybit/bybit_exchange.py`

```python
async def _place_order(self,
                       order_id: str,
                       trading_pair: str,
                       amount: Decimal,
                       trade_type: TradeType,
                       order_type: OrderType,
                       price: Decimal,
                       **kwargs) -> Tuple[str, float]:
    
    # Prepare API parameters
    api_params = {
        "category": self._category,        # spot, linear, inverse
        "symbol": symbol,                   # BTCUSDT
        "side": side_str,                   # Buy/Sell
        "orderType": type_str,              # Limit/Market
        "qty": f"{amount:f}",
        "price": f"{price:f}",
        "orderLinkId": order_id             # Client order ID
    }
    
    if order_type == OrderType.LIMIT:
        api_params["timeInForce"] = "GTC"
    
    # Send REST API request
    response = await self._api_post(
        path_url=CONSTANTS.ORDER_PLACE_PATH_URL,
        data=api_params,
        is_auth_required=True,
        trading_pair=trading_pair
    )
    
    if response["retCode"] != 0:
        raise ValueError(f"{response['retMsg']}")
    
    order_result = response.get("result", {})
    exchange_order_id = str(order_result["orderId"])
    transact_time = int(response["time"]) * 1e-3
    
    return (exchange_order_id, transact_time)
```

### 4.3 Order State Tracking

**File:** `hummingbot/connector/client_order_tracker.py`

```python
class ClientOrderTracker:
    def __init__(self):
        self._in_flight_orders: Dict[str, InFlightOrder] = {}
    
    def start_tracking_order(self, order: InFlightOrder):
        """Track new order"""
        self._in_flight_orders[order.client_order_id] = order
    
    def update_order_from_exchange_update(self, order_update: OrderUpdate):
        """Update from user stream WebSocket"""
        order = self._in_flight_orders.get(order_update.client_order_id)
        if order:
            order.update_with_order_update(order_update)
```

### 4.4 Order Lifecycle Events

**Event Flow:**

```
1. Strategy calls market.buy() / market.sell()
   ↓
2. Connector creates InFlightOrder and adds to order tracker
   ↓
3. Connector sends REST API request to exchange
   ↓
4. Exchange responds with exchange_order_id
   ↓
5. InFlightOrder updated with exchange_order_id
   ↓
6. Connector fires MarketEvent.BuyOrderCreated / SellOrderCreated
   ↓
7. Strategy receives event via BuyOrderCreatedListener
   ↓
8. User stream WebSocket receives order fill update
   ↓
9. Connector updates InFlightOrder state to FILLED
   ↓
10. Connector fires MarketEvent.OrderFilled
   ↓
11. Strategy receives event via OrderFilledListener
   ↓
12. Strategy processes fill in c_did_fill_order()
```

---

## Part 5: Event-Driven Architecture

### 5.1 PubSub Event System

**File:** `hummingbot/core/pubsub.pyx`

```cython
cdef class PubSub:
    def c_trigger_event(self, int event_tag, object message):
        """Fire event to all registered listeners"""
        cdef EventListener listener
        if event_tag not in self._event_listeners:
            return
        
        for listener in self._event_listeners[event_tag]:
            listener.c_call(message)
```

### 5.2 Strategy Event Listeners

**File:** `hummingbot/strategy/strategy_base.pyx`

```cython
# Event listener classes
cdef class OrderFilledListener(BaseStrategyEventListener):
    cdef c_call(self, object arg):
        self._owner.c_did_fill_order(arg)

cdef class BuyOrderCompletedListener(BaseStrategyEventListener):
    cdef c_call(self, object arg):
        self._owner.c_did_complete_buy_order(arg)
        self._owner.c_did_complete_buy_order_tracker(arg)

# Strategy implements event handlers
cdef c_did_fill_order(self, object order_filled_event):
    """Override in strategy subclass to handle fills"""
    pass

cdef c_did_complete_buy_order(self, object buy_order_completed_event):
    """Override in strategy subclass to handle completed buys"""
    pass
```

---

## Complete Lifecycle Summary

### Phase 1: Initialization (Once)
```
1. TradingCore creates ConnectorManager
2. ConnectorManager initializes Exchange connectors
3. Each connector creates:
   - OrderBookTracker → spawns WebSocket listeners
   - UserStreamTracker → spawns account event listeners
   - ClientOrderTracker → manages in-flight orders
4. Strategy is created and initialized
5. Strategy registers event listeners with connectors
6. Clock is created and adds connectors + strategy as children
```

### Phase 2: Data Streaming (Continuous)
```
OrderBookTrackerDataSource:
├─ WebSocket: listen_for_order_book_diffs()
│  └─ Streams to _order_book_diff_stream queue
│     └─ Router updates OrderBook objects
├─ WebSocket: listen_for_trades()
│  └─ Streams to _order_book_trade_stream queue
└─ WebSocket: listen_for_order_book_snapshots()
   └─ Streams to _order_book_snapshot_stream queue

UserStreamTrackerDataSource:
└─ WebSocket: listen_for_user_stream()
   └─ Receives: order fills, cancels, balance updates
      └─ ClientOrderTracker updates InFlightOrder states
         └─ Fires events: OrderFilled, OrderCancelled, etc.
```

### Phase 3: Strategy Tick (Every 1 second by default)
```
Clock.run():
  ├─ await asyncio.sleep(next_tick_time - now)
  └─ For each child_iterator:
      ├─ connector.c_tick(timestamp)
      │  └─ Poll balances, update order states
      └─ strategy.c_tick(timestamp)
          1. Check if markets ready (order books initialized)
          2. Read current market data:
             - Best bid/ask prices
             - Order book depth
             - Account balances
          3. Run strategy logic:
             - Create order proposals
             - Apply modifiers (inventory skew, etc.)
             - Filter by budget constraints
          4. Cancel orders:
             - Orders exceeding max age
             - Orders outside new price range
          5. Place new orders:
             - Call connector.buy() / connector.sell()
```

### Phase 4: Order Execution (Async)
```
Strategy calls connector.buy():
  1. connector.c_quantize_order_amount() → round to exchange precision
  2. connector.c_quantize_order_price() → round to tick size
  3. Create InFlightOrder object
  4. ClientOrderTracker.start_tracking_order()
  5. Send REST API request: _place_order()
     ├─ Build API params (symbol, side, qty, price, etc.)
     ├─ Sign request with API key
     └─ POST to exchange API
  6. Receive response:
     ├─ Extract exchange_order_id
     └─ Update InFlightOrder.exchange_order_id
  7. Fire event: MarketEvent.BuyOrderCreated
  8. Strategy receives event via listener
```

### Phase 5: Order Updates (Async via WebSocket)
```
Exchange sends fill update → UserStreamTracker:
  1. Parse WebSocket message
  2. Extract:
     - exchange_order_id or client_order_id
     - fill_price, fill_quantity
     - order_status (FILLED, PARTIALLY_FILLED, etc.)
  3. ClientOrderTracker.update_order_from_exchange_update()
  4. Update InFlightOrder:
     ├─ current_state = OrderState.FILLED
     ├─ executed_amount_base += fill_quantity
     └─ average_executed_price = updated
  5. Fire event: MarketEvent.OrderFilled
  6. Strategy receives event:
     ├─ c_did_fill_order(order_filled_event)
     └─ Update internal state (positions, PnL, etc.)
```

---

## Key Design Patterns

### 1. **Clock-Based Synchronous Ticks**
- All components tick at regular intervals (default 1s)
- Strategy logic runs synchronously in c_tick()
- Order placement/cancellation is async (fire-and-forget)

### 2. **Event-Driven Updates**
- Market data updates via WebSocket (async)
- Order state updates via WebSocket (async)
- Strategy reacts to events via listeners

### 3. **Separation of Concerns**
- **Connector:** Market data + Order execution
- **Strategy:** Trading logic + Risk management
- **Clock:** Time coordination
- **EventSystem:** Decoupled communication

### 4. **Async Task Management**
- Each connector spawns multiple async tasks
- Tasks run concurrently in event loop
- Clock ticks happen in sync with asyncio.sleep()

### 5. **State Machine (InFlightOrder)**
```
PENDING_CREATE → OPEN → PARTIALLY_FILLED → FILLED
                  ↓
              PENDING_CANCEL → CANCELLED
```

---

## Performance Considerations

### Market Data Latency
```
Exchange → WebSocket → OrderBookTracker → OrderBook (in-memory)
         ~10-50ms      ~1-5ms              instant access
```

### Order Placement Latency
```
Strategy → Connector → REST API → Exchange
 c_tick()   async     ~50-200ms
```

### Memory Management
- Order books stored as Pandas DataFrames
- Cython for performance-critical paths (c_tick, event dispatch)
- Async I/O prevents blocking on network calls

---

## Comparison to Your C++ Implementation

| Aspect | Hummingbot (Python/Cython) | Your latentspeed (C++) |
|--------|---------------------------|------------------------|
| **Market Data** | WebSocket → asyncio.Queue → OrderBook | WebSocket → Lock-free queue → MarketData |
| **Event Loop** | asyncio (single-threaded) | Multi-threaded (dedicated per connector) |
| **Strategy Tick** | Clock.c_tick() calls strategy.c_tick() | Timer-based or event-driven |
| **Order Execution** | REST API (async) | REST API + WebSocket confirmations |
| **State Management** | InFlightOrder (state machine) | Order state tracking in C++ |
| **Performance** | ~1ms tick latency (Cython) | <100μs (native C++) |
| **Memory** | Pandas DataFrames | Pre-allocated memory pools |

**Key Takeaway for Your Implementation:**
- Hummingbot uses a **hybrid approach**: synchronous strategy ticks + async I/O
- Your C++ engine can achieve **lower latency** with lock-free queues and zero-copy
- Consider adopting the **Clock-based tick pattern** for strategy execution
- Event-driven updates via WebSocket (like Hummingbot) are essential for real-time fills

---

## References

**Core Files:**
- `hummingbot/core/trading_core.py` - Main orchestrator
- `hummingbot/core/clock.pyx` - Time-based event system
- `hummingbot/strategy/strategy_base.pyx` - Strategy base class
- `hummingbot/connector/exchange_py_base.py` - Exchange connector base
- `hummingbot/core/data_type/order_book_tracker.py` - Market data streaming
- `hummingbot/connector/client_order_tracker.py` - Order state management

**Example Strategy:**
- `hummingbot/strategy/pure_market_making/pure_market_making.pyx`
