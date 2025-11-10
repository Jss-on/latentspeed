# arkpad-ahab2 vs Hummingbot: Strategy Architecture Comparison

## Executive Summary

| Aspect | **Hummingbot** | **arkpad-ahab2** |
|--------|----------------|------------------|
| **Architecture** | Clock-based synchronous ticks | Clean Architecture + DI |
| **Language** | Python + Cython | Pure Python 3.10+ |
| **Strategy Pattern** | Inheritance (StrategyBase) | Interface-based (IStrategy) |
| **Event Model** | PubSub + EventListeners | Direct async calls |
| **Order Management** | Embedded in connector | Central OrderManager |
| **Time** | Clock.c_tick() | TimeProvider abstraction |
| **Coupling** | Tight | Loose (interfaces) |

---

## 1. Core Architecture

### Hummingbot: Clock-Driven

```@\\wsl.localhost\Ubuntu-22.04\home\tensor\latentspeed\sub\hummingbot\hummingbot\core\clock.pyx#85:129
async def run(self):
    await self.run_til(float("nan"))

async def run_til(self, timestamp: float):
    cdef:
        TimeIterator child_iterator
        double now = time.time()
        double next_tick_time

    if self._current_context is None:
        raise EnvironmentError("run() and run_til() can only be used within the context of a `with...` statement.")

    self._current_tick = (now // self._tick_size) * self._tick_size
    if not self._started:
        for ci in self._current_context:
            child_iterator = ci
            child_iterator.c_start(self, self._current_tick)
        self._started = True

    try:
        while True:
            now = time.time()
            if now >= timestamp:
                return

            # Sleep until the next tick
            next_tick_time = ((now // self._tick_size) + 1) * self._tick_size
            await asyncio.sleep(next_tick_time - now)
            self._current_tick = next_tick_time

            # Run through all the child iterators.
            for ci in self._current_context:
                child_iterator = ci
                try:
                    child_iterator.c_tick(self._current_tick)
                except StopIteration:
                    self.logger().error("Stop iteration triggered in real time mode. This is not expected.")
                    return
                except Exception:
                    self.logger().error("Unexpected error running clock tick.", exc_info=True)
    finally:
        for ci in self._current_context:
            child_iterator = ci
            child_iterator._clock = None
```

**Key:** Clock ticks all children (connectors + strategies) every 1s.

### arkpad-ahab2: Event-Driven

```@\\wsl.localhost\Ubuntu-22.04\home\tensor\latentspeed\sub\arkpad-ahab2\trading_core\runtime\core.py#20:64
class CleanTradingRuntime:
    """Orchestrates the startup, execution, and shutdown of high-level trading components."""

    def __init__(
        self,
        market_data_source: MarketDataSource,
        execution_client: ExecutionClientInterface,
        strategy_host: "IStrategyHost",
        managed_services: List[IManagedService] = None
    ):
        self.market_data_source = market_data_source
        self.execution_client = execution_client
        self.strategy_host = strategy_host
        self.managed_services = managed_services or []
        self._shutdown_event = asyncio.Event()

    async def run(self):
        """Runs the trading system until a shutdown signal is received."""
        try:
            await self.start()
            await self._shutdown_event.wait()
        except Exception as e:
            # In a real implementation, log this exception
            pass
        finally:
            await self.stop()

    async def start(self):
        """Starts all managed components in the correct order."""
        for service in self.managed_services:
            await service.start()
        await self.execution_client.start()
        await self.strategy_host.start()
        await self.market_data_source.start()

    async def stop(self):
        """Stops all managed components gracefully."""
        await self.market_data_source.stop()
        await self.strategy_host.stop()
        await self.execution_client.stop()
        for service in reversed(self.managed_services):
            await service.stop()

    def signal_shutdown(self):
        self._shutdown_event.set()
```

**Key:** No clock, pure async with dependency injection.

---

## 2. Strategy API

### Hummingbot: StrategyBase

```@\\wsl.localhost\Ubuntu-22.04\home\tensor\latentspeed\sub\hummingbot\hummingbot\strategy\strategy_base.pyx#117:181
cdef class StrategyBase(TimeIterator):
    BUY_ORDER_COMPLETED_EVENT_TAG = MarketEvent.BuyOrderCompleted.value
    SELL_ORDER_COMPLETED_EVENT_TAG = MarketEvent.SellOrderCompleted.value
    FUNDING_PAYMENT_COMPLETED_EVENT_TAG = MarketEvent.FundingPaymentCompleted.value
    POSITION_MODE_CHANGE_SUCCEEDED_EVENT_TAG = AccountEvent.PositionModeChangeSucceeded.value
    POSITION_MODE_CHANGE_FAILED_EVENT_TAG = AccountEvent.PositionModeChangeFailed.value
    ORDER_FILLED_EVENT_TAG = MarketEvent.OrderFilled.value
    ORDER_CANCELED_EVENT_TAG = MarketEvent.OrderCancelled.value
    ORDER_EXPIRED_EVENT_TAG = MarketEvent.OrderExpired.value
    ORDER_FAILURE_EVENT_TAG = MarketEvent.OrderFailure.value
    BUY_ORDER_CREATED_EVENT_TAG = MarketEvent.BuyOrderCreated.value
    SELL_ORDER_CREATED_EVENT_TAG = MarketEvent.SellOrderCreated.value
    RANGE_POSITION_LIQUIDITY_ADDED_EVENT_TAG = MarketEvent.RangePositionLiquidityAdded.value
    RANGE_POSITION_LIQUIDITY_REMOVED_EVENT_TAG = MarketEvent.RangePositionLiquidityRemoved.value
    RANGE_POSITION_UPDATE_EVENT_TAG = MarketEvent.RangePositionUpdate.value
    RANGE_POSITION_UPDATE_FAILURE_EVENT_TAG = MarketEvent.RangePositionUpdateFailure.value
    RANGE_POSITION_FEE_COLLECTED_EVENT_TAG = MarketEvent.RangePositionFeeCollected.value
    RANGE_POSITION_CLOSED_EVENT_TAG = MarketEvent.RangePositionClosed.value


    @classmethod
    def logger(cls) -> logging.Logger:
        raise NotImplementedError

    def __init__(self):
        super().__init__()
        self._sb_markets = set()
        self._sb_create_buy_order_listener = BuyOrderCreatedListener(self)
        self._sb_create_sell_order_listener = SellOrderCreatedListener(self)
        self._sb_fill_order_listener = OrderFilledListener(self)
        self._sb_fail_order_listener = OrderFailedListener(self)
        self._sb_cancel_order_listener = OrderCancelledListener(self)
        self._sb_expire_order_listener = OrderExpiredListener(self)
        self._sb_complete_buy_order_listener = BuyOrderCompletedListener(self)
        self._sb_complete_sell_order_listener = SellOrderCompletedListener(self)
        self._sb_complete_funding_payment_listener = FundingPaymentCompletedListener(self)
        self._sb_position_mode_change_success_listener = PositionModeChangeSuccessListener(self)
        self._sb_position_mode_change_failure_listener = PositionModeChangeFailureListener(self)
        self._sb_range_position_liquidity_added_listener = RangePositionLiquidityAddedListener(self)
        self._sb_range_position_liquidity_removed_listener = RangePositionLiquidityRemovedListener(self)
        self._sb_range_position_update_listener = RangePositionUpdateListener(self)
        self._sb_range_position_update_failure_listener = RangePositionUpdateFailureListener(self)
        self._sb_range_position_fee_collected_listener = RangePositionFeeCollectedListener(self)
        self._sb_range_position_closed_listener = RangePositionClosedListener(self)

        self._sb_delegate_lock = False

        self._sb_order_tracker = OrderTracker()

    def init_params(self, *args, **kwargs):
        """
        Assigns strategy parameters, this function must be called directly after init.
        The reason for this is to make the parameters discoverable through introspect (this is not possible on init of
        a Cython class).
        """
        raise NotImplementedError

    @property
    def active_markets(self) -> List[ConnectorBase]:
        return list(self._sb_markets)

    @property
    def order_tracker(self) -> OrderTracker:
        return self._sb_order_tracker
```

- Inherits from `TimeIterator`
- Implements `c_tick()` method
- Event listeners for market events

### arkpad-ahab2: IStrategy Interface

```@\\wsl.localhost\Ubuntu-22.04\home\tensor\latentspeed\sub\arkpad-ahab2\trading_core\strategy\api.py#8:49
class StrategyBase(IStrategy):
    """
    Minimal Strategy API for Phase 1.
    Override the event handlers you need. Keep handlers fast and non-blocking.
    Do heavier work in timers or background tasks.
    
    Implements IStrategy interface for dependency inversion.
    """
    name: str = "base"
    config_class: Optional[Type] = None  # Define config schema for this strategy

    def init(self, ctx: IStrategyContext) -> None:
        """Called once at startup. Save the context and set up timers if needed."""
        self.ctx = ctx  # type: ignore[attr-defined]

    async def on_trade(self, msg: Dict[str, Any]) -> None:
        """Called for each preprocessed trade message (dict from ZMQ)."""
        return

    async def on_book(self, msg: Dict[str, Any]) -> None:
        """Called for each preprocessed book message (dict from ZMQ)."""
        return

    async def on_timer(self, now_ns: int) -> None:
        """Called when a timer you scheduled with ctx.every_ms() fires."""
        return

    async def on_fill(self, fill: Dict[str, Any]) -> None:
        """Called when a fill is reported (wired later in Phase 3)."""
        return

    def risk_limits(self) -> Dict[str, Any]:
        """
        Optional: return stricter per-strategy limits (tighten-only)
        that the risk engine may apply. Leave empty for Phase 1.
        """
        return {}


# LegacyStrategyContext removed. Use CleanStrategyContext via
# trading_core.runtime.strategy_context_impl.CleanStrategyContext.
```

- Interface-based (`IStrategy`)
- Event handlers: `on_book()`, `on_trade()`, `on_timer()`, `on_fill()`
- Access via `IStrategyContext`

---

## 3. Strategy Context

### Hummingbot: Direct Market Access

Strategy holds direct reference to `ConnectorBase`:

```python
# Inside strategy
market = self._market_info.market
bid = market.get_price(pair, False)
balance = market.get_balance(asset)
order_id = market.buy(pair, amount, order_type, price)
```

### arkpad-ahab2: Context Interface

```@\\wsl.localhost\Ubuntu-22.04\home\tensor\latentspeed\sub\arkpad-ahab2\trading_core\runtime\strategy_context_impl.py#19:66
class CleanStrategyContext(IStrategyContext):
    """A clean, interface-based implementation of the strategy context.
    
    This class contains no business logic. It acts as a dependency-injected
    gateway to other system components, following clean architecture principles.
    """
    def __init__(
        self,
        account_view: IAccountView,
        order_manager: IOrderManager,
        market_data_view: IMarketDataView,
        time_provider: Optional[object] = None,
        rules_map: Optional[Dict[str, SymbolRule]] = None,
    ):
        self._account = account_view
        self._order_manager = order_manager
        self._market_data = market_data_view
        self._time_provider = time_provider
        # store shallow copy to keep context isolated
        self._symbol_rules: Dict[str, SymbolRule] = dict(rules_map or {})
        # Timer scheduler hooks (injected by StrategyHost)
        self._schedule_timer_fn: Optional[Callable[[int, Callable[[int], Awaitable[None]]], str]] = None
        self._cancel_timer_fn: Optional[Callable[[str], bool]] = None
        self._pending_timers: list[tuple[int, Callable[[int], Awaitable[None]]]] = []

    def get_market_price(self, symbol: str) -> Optional[float]:
        """Delegates price requests to the market data view."""
        return self._market_data.get_mid_price(symbol)

    def get_position(self, symbol: str) -> Optional[Position]:
        """Delegates position requests to the account view."""
        return self._account.get_position(symbol)

    def get_available_balance(self, asset: str) -> float:
        """Delegates balance requests to the account view."""
        balance = self._account.get_balance(asset)
        return balance.available if balance else 0.0

    async def submit_order(self, order: ProposedOrder) -> OrderSubmissionResult:
        """Delegates order submission to the order manager."""
        return await self._order_manager.submit_order(order)

    async def cancel_order(self, cancel: CancelOrder) -> bool:
        """Delegates order cancellation to the order manager."""
        try:
            return await self._order_manager.cancel_order(cancel)  # type: ignore[attr-defined]
        except Exception:
            return False
```

Strategy accesses everything via context:

```python
mid = self.ctx.get_market_price("BTC-USDT")
balance = self.ctx.get_available_balance("USDT")
result = await self.ctx.submit_order(order)
```

---

## 4. Order Management

### Hummingbot: Embedded in Connector

Orders flow directly from strategy → connector → exchange.

### arkpad-ahab2: Central OrderManager

Orders flow: strategy → context → **OrderManager** → risk → execution

**Benefits:**
- Central risk enforcement
- Balance locking (prevents over-trading)
- Order lifecycle tracking
- Easy to test

---

## 5. Key Takeaways for Your C++ Implementation

### From Hummingbot
✅ **Clock-based ticks** for deterministic strategy execution  
✅ **Async WebSocket streams** for market data  
✅ **Order state machine** pattern (InFlightOrder)  

### From arkpad-ahab2
✅ **Interface-based design** (abstract base classes in C++)  
✅ **Central OrderManager** with risk enforcement  
✅ **Balance locking** to prevent over-trading  
✅ **TimeProvider abstraction** for backtests  
✅ **Dependency injection** for testability  

### Recommended Hybrid Architecture for C++

```cpp
// Interfaces (similar to arkpad-ahab2)
class IStrategyContext {
public:
    virtual double get_market_price(const std::string& symbol) = 0;
    virtual double get_available_balance(const std::string& asset) = 0;
    virtual OrderResult submit_order(const ProposedOrder& order) = 0;
};

// Clock-based coordinator (similar to Hummingbot)
class TradingEngine {
    void tick(uint64_t timestamp_ns) {
        // 1. Tick all connectors (update order states)
        for (auto& connector : connectors_) {
            connector->tick(timestamp_ns);
        }
        
        // 2. Tick all strategies
        for (auto& strategy : strategies_) {
            strategy->tick(timestamp_ns);
        }
    }
};

// Strategy base class
class StrategyBase {
protected:
    IStrategyContext* ctx_;
    
public:
    virtual void init(IStrategyContext* ctx) {
        ctx_ = ctx;
    }
    
    virtual void tick(uint64_t timestamp_ns) {
        // Override in subclass
    }
    
    virtual void on_book_update(const BookUpdate& update) {
        // Override if needed
    }
    
    virtual void on_fill(const Fill& fill) {
        // Override if needed
    }
};
```

This gives you:
- **Deterministic execution** (clock-based)
- **Loose coupling** (interface-based)
- **Central risk** (OrderManager)
- **High performance** (C++ native)
