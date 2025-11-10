# Lead-Lag Integration Report: trading_core Architecture and Usage Guide

**Generated**: 2025-10-28  
**Version**: 1.0  
**Scope**: Comprehensive analysis of lead-lag strategy implementation within the trading_core architecture

---

## Table of Contents

1. [Glossary of Terms](#glossary-of-terms) ⭐ **Start here if terms are confusing**
2. [How to Read This Report](#how-to-read-this-report) 📖 **Reading guide with real example**
3. [Executive Summary](#executive-summary)
4. [Architecture Overview](#architecture-overview)
5. [Data Flow Pipeline](#data-flow-pipeline)
6. [Lead-Lag Strategy Implementation](#lead-lag-strategy-implementation)
7. [Risk Management Integration](#risk-management-integration)
8. [Order Execution Flow](#order-execution-flow)
9. [Configuration Guide](#configuration-guide)
10. [Deployment Instructions](#deployment-instructions)
11. [Code References](#code-references)
12. [Troubleshooting](#troubleshooting)
13. [Recommendations](#recommendations)

---

## Glossary of Terms

### Trading & Finance Terms

- **Lead-Lag**: When one asset's price moves before another correlated asset. Example: Bitcoin moves up, then Ethereum follows 2 seconds later.
- **Arbitrage**: Profiting from price differences between related assets or markets.
- **Perpetual (Perp)**: A futures contract with no expiration date. You can hold it forever.
- **Spot**: Buying/selling the actual asset immediately (not a futures contract).
- **Long/Buy**: Betting the price will go up.
- **Short/Sell**: Betting the price will go down.
- **Leverage**: Borrowing money to increase position size. 10x leverage = control $10,000 with $1,000.
- **Initial Margin (IM)**: The collateral/deposit required to open a leveraged position.
- **Reduce-Only**: An order that can only close (not increase) an existing position.
- **Market Order**: Buy/sell immediately at current market price.
- **Limit Order**: Buy/sell only at a specific price or better.
- **Stop-Loss (SL)**: Automatic exit order to limit losses if price moves against you.
- **Take-Profit (TP)**: Automatic exit order to lock in gains at target price.
- **Time-in-Force (TIF)**: How long an order stays active (IOC = cancel if not filled immediately, GTC = stay active until filled).
- **Notional**: The total dollar value of a position (price × quantity).
- **Basis Points (bps)**: 1/100th of 1%. So 100 bps = 1%, 5 bps = 0.05%.
- **Slippage**: The difference between expected price and actual execution price.
- **Liquidity**: How easy it is to buy/sell without moving the price much.

### Strategy Terms

- **Hayashi-Yoshida (HY) Estimator**: A statistical method to measure correlation between two assets even when their price updates don't happen at the same time.
- **Correlation (rho*)**: How closely two assets move together. +1 = perfect same direction, -1 = perfect opposite, 0 = no relationship.
- **Optimal Lag (τ*)**: The time delay between a leader's move and lagger's move. Example: BTC moves, ETH follows 1.5 seconds later.
- **Lead-Lag Strength (LLS)**: How strong/reliable the lead-lag relationship is.
- **Jump Detection**: Identifying sudden, significant price movements (not normal noise).
- **Co-Jump**: Multiple assets jumping at the same time (usually means market-wide news).
- **ATR (Average True Range)**: Measures how much an asset's price typically moves. Used to set dynamic stop-loss/take-profit levels.
- **Expected Move**: How much we predict the lagger will move after the leader jumps.
- **Feature**: A calculated value from market data (like order book imbalance or trade flow).
- **OFI (Order Flow Imbalance)**: Net buying vs selling pressure in the order book.
- **Signal Strength**: How confident we are in the trading opportunity (higher = stronger).
- **Entry Threshold**: Minimum signal strength required to take a trade.
- **Hazard Window**: Cooldown period after a trade to avoid overtrading the same symbol.

### Technical Architecture Terms

- **ZMQ (ZeroMQ)**: A fast messaging library for sending data between programs. Like a super-fast postal service.
- **Pub/Sub (Publisher/Subscriber)**: One program publishes data, many programs can subscribe to receive it. Like a news feed.
- **Topic**: A category/channel for messages. Example: "BTC-trades" topic only has Bitcoin trade data.
- **Port**: A numbered door on your computer where programs connect. Example: port 5556 for trade data.
- **TCP**: The reliable internet protocol for sending data between computers.
- **WebSocket**: A two-way communication channel (like a phone call vs sending letters).
- **Socket**: The connection endpoint between two programs.
- **Pipeline**: A series of processing steps where output of one becomes input of the next.
- **Gateway**: The entry point that receives data from external sources.
- **Context**: An object that gives a strategy access to market data and order submission functions.

### Software Architecture Terms

- **Clean Architecture**: Organizing code so each part has one job and parts can be easily swapped.
- **Interface**: A contract defining what methods a component must provide (but not how it does it).
- **Dependency Inversion**: High-level code doesn't depend on low-level details; both depend on interfaces.
- **Factory**: A component that creates/builds other components based on configuration.
- **Runtime**: The environment and orchestrator that runs your strategy.
- **Executor**: A component that manages a single position's lifecycle (entry, stop-loss, take-profit, exit).
- **DTO (Data Transfer Object)**: A simple container for passing data between layers (no logic, just data).
- **Schema**: The structure/format that data must follow (like a template).
- **Pydantic**: A Python library for validating data matches the expected schema.
- **YAML**: A human-readable configuration file format (like JSON but easier to read).

### System Components

- **Marketstream**: The C++ program that collects live market data from exchanges and publishes it.
- **Trading Core**: The Python framework that runs strategies, manages risk, and coordinates everything.
- **Trading Engine Service**: The C++ program that sends orders to exchanges and reports back results.
- **Strategy**: Your trading logic/algorithm that decides when to buy/sell.
- **OrderManager**: The component that checks your orders are valid and safe before sending.
- **RiskEngine**: The component with rules to prevent dangerous trades (too big, too fast, bad price, etc.).
- **ExecutionClient**: The component that sends orders to the trading engine and receives reports back.
- **PriceView**: A component that tracks the latest prices for all symbols.
- **HistoryView**: A component that stores recent trade/book history for analysis.

### Risk Management Terms

- **Stale Data**: Market data that's too old to be trusted for trading decisions.
- **Price Band**: The acceptable price range for an order (reject if price is too far from mid).
- **Rate Limiting**: Preventing too many orders per second (to avoid overwhelming systems or triggering exchange limits).
- **Inventory Band**: Maximum position size limits per symbol.
- **Gross Exposure**: Total dollar value of all positions (long + short, ignoring direction).
- **Circuit Breaker**: Emergency stop that halts trading if losses exceed threshold.
- **Queue Model**: Simulating order book dynamics to estimate fill probability in backtests.

### Database/Storage Terms

- **PostgreSQL**: A powerful open-source database for storing structured data.
- **TimescaleDB**: A PostgreSQL extension optimized for time-series data (like market data).
- **Batch Size**: How many rows to load from database at once (bigger = faster but more memory).
- **Replay**: Playing back historical data as if it's happening live now (for backtesting).

### Backtest Terms

- **Backtest**: Testing a strategy on historical data to see how it would have performed.
- **Speed Multiplier**: How much faster than real-time to run the backtest. 1000x = 1 day replayed in ~86 seconds.
- **Simulation**: Mimicking real trading conditions (fees, slippage, partial fills) in a backtest.
- **Paper Trading**: Testing with live data but not real money (simulated account).
- **Fill Probability**: Chance that a limit order gets filled (based on queue position).
- **Adverse Selection**: When your limit order fills, the price immediately moves against you (you got "picked off").

### Monitoring Terms

- **Health Check**: An endpoint that reports if a system is running properly.
- **Metrics**: Numbers tracking system performance (trades filled, errors, active positions, etc.).
- **Structured Logging**: Logs in a machine-readable format (JSON) for easy searching/analysis.
- **Prometheus/Grafana**: Tools for collecting metrics and displaying them on dashboards.

### Order Lifecycle Terms

- **Proposed Order**: An order the strategy wants to place (before risk checks).
- **Execution Order**: An order that passed risk checks and is being sent to the exchange.
- **Execution Report**: A message from the exchange saying order was accepted/rejected/canceled.
- **Fill**: A message from the exchange saying your order was executed (fully or partially).
- **Acknowledgment (ACK)**: Confirmation from exchange that they received your order.
- **Client Order ID (cl_id)**: Your unique ID for tracking an order (not the exchange's ID).

---

## How to Read This Report

**If you're new to trading systems**, follow this order:

1. **Start with the Glossary above** - Bookmark it and refer back when you encounter unfamiliar terms
2. **Read the Executive Summary** - Get the big picture of what this system does
3. **Look at the Architecture Overview diagrams** - Understand how pieces connect
4. **Skip to Deployment Instructions** - Get the system running first (learning by doing)
5. **Return to detailed sections** - Dive deeper into specific areas as needed

**If you're technical but new to trading**, focus on:
- Glossary → Architecture Overview → Data Flow Pipeline → Code References

**If you're a trader but new to technical systems**, focus on:
- Glossary → Lead-Lag Strategy Implementation → Configuration Guide → Deployment

**Quick Concept Summary:**

This system watches multiple cryptocurrencies in real-time. When one coin (the "leader") makes a sudden price jump, the system predicts if another related coin (the "lagger") will follow. If the relationship is strong enough, it automatically places a trade to profit from the expected move. The system has safety checks (risk management) to prevent bad trades and automatically exits positions with stop-losses and take-profits.

**Real-World Example:**
1. Bitcoin jumps up by $200 in 1 second (detected by Jump Detection)
2. System checks: "Does Ethereum usually follow Bitcoin?" (Hayashi-Yoshida Estimator)
3. System finds: "Yes! Ethereum follows Bitcoin 1.5 seconds later with 0.85 correlation"
4. System calculates: "Ethereum should move ~$10 based on this" (Expected Move Predictor)
5. System checks risk rules: "Do we have enough money? Is this trade safe?" (RiskEngine)
6. System places trade: "Buy Ethereum now with stop-loss at -$5, take-profit at +$15" (PositionExecutor)
7. System monitors: If profit target hit → close and take profit. If stop-loss hit → close and limit loss.

---

## Executive Summary

This report documents the **lead-lag arbitrage strategy** implementation within the `arkpad-ahab2/trading_core` Python framework and its integration with the latentspeed C++ high-frequency trading engine. The system implements a complete pipeline from market data ingestion through strategy execution to order placement.

### Key Findings

- **Architecture**: Clean architecture with dependency inversion, supporting both live trading and backtesting
- **Lead-Lag Strategy**: `AdaptiveLeadLagStrategy` uses online Hayashi-Yoshida estimator for real-time relationship detection
- **Data Flow**: ZMQ-based market data → strategy analysis → risk validation → C++ execution engine
- **Execution**: Supports both spot and perpetuals with ATR-based dynamic brackets and fee-aware gating

### System Components

| Component | Technology | Purpose |
|-----------|-----------|---------|
| `marketstream` | C++ (latentspeed) | Market data aggregation and publishing |
| `trading_core` | Python | Strategy runtime, risk management, order orchestration |
| `trading_engine_service` | C++ (latentspeed) | Order execution and venue connectivity |
| Communication | ZeroMQ | Low-latency pub/sub messaging |
| Storage | PostgreSQL/TimescaleDB | Historical data for backtesting |

---

## Architecture Overview

### Simple Overview

**Think of this system as a factory production line:**

1. **Raw Materials (Market Data)** → Marketstream collects price/trade data from exchanges
2. **Inspection Station (Gateway)** → Checks data quality and organizes it
3. **Decision Maker (Strategy)** → Analyzes data and decides "should we trade?"
4. **Quality Control (Risk Management)** → Checks "is this trade safe?"
5. **Shipping Department (Execution)** → Sends orders to exchanges
6. **Feedback Loop** → Reports back what happened (filled, rejected, etc.)

**Three Main Programs Working Together:**
- **marketstream** (C++): Fast data collector → publishes market updates
- **trading_core** (Python): Smart decision maker → runs your strategy and safety checks
- **trading_engine_service** (C++): Fast order sender → talks to exchanges

**Communication:** They talk via ZMQ (like a super-fast internal mail system with specific mailbox numbers called "ports")

### High-Level System Design

```
┌─────────────────────────────────────────────────────────────────┐
│                        MARKET DATA LAYER                         │
│  ┌────────────────┐         ZMQ PUB/SUB          ┌────────────┐ │
│  │  marketstream  │────────────────────────────▶ │ ZMQ Topics │ │
│  │  (C++ Engine)  │  port 5556 (trades)         │ Filtered   │ │
│  │                │  port 5557 (books)          │ by Symbol  │ │
│  └────────────────┘                              └────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                       TRADING CORE LAYER                         │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │              MarketDataGateway                            │   │
│  │  • Subscribes to ZMQ topics                              │   │
│  │  • Updates PriceView & HistoryView                       │   │
│  │  • Dispatches to StrategyHost                            │   │
│  └──────────────────────────────────────────────────────────┘   │
│                              │                                   │
│                              ▼                                   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │          AdaptiveLeadLagStrategy                          │   │
│  │  • on_trade: HY estimator updates, jump detection        │   │
│  │  • on_book: ATR computation, feature updates             │   │
│  │  • Decision: aggregate features → entry signal           │   │
│  └──────────────────────────────────────────────────────────┘   │
│                              │                                   │
│                              ▼                                   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │              OrderManager                                 │   │
│  │  1. RiskEngine validation                                │   │
│  │  2. Balance checks + IM/fee reservation (perps)          │   │
│  │  3. Fund locking (spot) / skip (perps)                   │   │
│  │  4. ProposedOrder → ExecutionOrder conversion            │   │
│  └──────────────────────────────────────────────────────────┘   │
│                              │                                   │
│                              ▼                                   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │          ExecClient (ZMQ PUSH/SUB)                        │   │
│  │  • PUSH ExecutionOrder → port 5601                       │   │
│  │  • SUB exec.report/fill ← port 5602                      │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      EXECUTION LAYER                             │
│  ┌────────────────────────────────────────────────────────┐     │
│  │    trading_engine_service (C++)                        │     │
│  │  • Receives orders via ZMQ                             │     │
│  │  • Routes to venue adapters (Bybit, Binance, etc.)     │     │
│  │  • Publishes execution reports and fills               │     │
│  └────────────────────────────────────────────────────────┘     │
└─────────────────────────────────────────────────────────────────┘
```

### Core Principles

1. **Clean Architecture**: Dependency inversion via interfaces (`IStrategyContext`, `IOrderManager`, `ExecutionClientInterface`)
2. **Live/Backtest Parity**: Same strategy code runs in simulation and production
3. **Separation of Concerns**: Market data, strategy logic, risk, and execution are decoupled
4. **Event-Driven**: Asynchronous message handling throughout the pipeline

### Port Numbers Quick Reference

**Think of ports like TV channels - each channel/port has specific content:**

| Port | Direction | Content | Who Uses It |
|------|-----------|---------|-------------|
| **5556** | marketstream → trading_core | Trade data (BTC traded at $50,000) | Strategy subscribes to watch prices |
| **5557** | marketstream → trading_core | Order book data (bid/ask prices) | Strategy subscribes for liquidity info |
| **5601** | trading_core → trading_engine | Your orders to send (Buy 1 BTC) | OrderManager sends orders here |
| **5602** | trading_engine → trading_core | Order updates (Order filled! / Order rejected) | OrderManager listens for results |
| **8082** | trading_core HTTP | Health check & metrics (Is system OK?) | You check this in browser/curl |
| **5562** | Optional: account sync | Account balance updates | Real-time position tracking |

**In Config Files:**
- `market_data.port: 5556` ← where to listen for trades
- `market_data.books_port: 5557` ← where to listen for order books  
- `execution.orders_port: 5601` ← where to send your orders
- `execution.events_port: 5602` ← where to get order results
- `observability.health_server.port: 8082` ← where to check system health

---

## Data Flow Pipeline

### Simple Explanation: Following One Trade From Start to Finish

**Imagine ordering pizza - here's how our system is similar:**

1. **You see a pizza deal** (Market Data) → System sees BTC price jump
2. **You decide to order** (Strategy) → Algorithm decides "this is a good opportunity"
3. **Check your wallet** (Risk) → System checks "do we have enough money? Is order safe?"
4. **Place the order** (Execution) → System sends order to exchange
5. **Get confirmation** (Fill) → Exchange says "order filled!" or "rejected"
6. **Monitor delivery** (Position Management) → Watch if profit target or stop-loss is hit

### Marketstream → Strategy → Risk → Orders

The complete data flow follows this path:

```
Market Data Ingestion → Gateway Processing → Strategy Analysis → 
Risk Validation → Order Conversion → Execution
```

**Step-by-Step with Real Example:**

**STEP 1**: BTC price jumps from $50,000 to $50,200 (+$200 in 1 second)  
**STEP 2**: Marketstream publishes "BTC-trade: $50,200" to port 5556  
**STEP 3**: Gateway receives it, updates PriceView, sends to Strategy  
**STEP 4**: Strategy detects jump, checks if ETH usually follows BTC  
**STEP 5**: Strategy finds strong correlation (0.85), predicts ETH will move $10  
**STEP 6**: Strategy creates order: "Buy ETH, stop-loss -$5, take-profit +$15"  
**STEP 7**: OrderManager asks RiskEngine "is this safe?"  
**STEP 8**: RiskEngine checks: price OK? balance OK? position limit OK? → APPROVED  
**STEP 9**: Order sent to trading_engine via port 5601  
**STEP 10**: Trading_engine sends order to Bybit/Binance exchange  
**STEP 11**: Exchange reports back "order filled!" via port 5602  
**STEP 12**: Strategy monitors position, exits when TP/SL hit or time expires

### 1. Market Data Ingestion

**Component**: `ZMQSubscriber` / `PostgreSQLMarketDataSource`  
**File**: `trading_core/bus/zmq_bus.py`, `trading_core/data/market_data.py`

**Live Mode (ZMQ)**:
- Subscribes to preprocessed market data topics
- Topic format: `{EXCHANGE}-preprocessed_{type}-{symbol}`
  - Example: `BINANCE_FUTURES-preprocessed_trades-BTC-USDT-PERP`
- Filters built dynamically from strategy symbols and configured exchanges
- Supports split-publisher architecture (trades on port 5556, books on port 5557)

**Code Reference**:
```python
# trading_core/runtime/factory.py (LiveRuntimeFactory.create_market_source)
src = ZMQMarketDataSource(
    host=md['host'],
    port=md['port'],
    filters=_filters,
    extra_endpoints=extras or None,  # books_port, index, funding
)
```

**Backtest Mode (PostgreSQL)**:
- Loads historical data from TimescaleDB
- Replays events in chronological order
- Simulated time provider for deterministic execution

### 2. Gateway Processing

**Component**: `MarketDataGateway`  
**File**: `trading_core/runtime/market_data_gateway.py`

**Responsibilities**:
1. Subscribe to market data source
2. Update views (PriceView, HistoryView) with latest snapshots
3. Optional L2 delta reconstruction (sparse deltas → full book snapshots)
4. Microstructure enrichment (volatility_mid, OFI, depth_n)
5. Dispatch events to StrategyHost

**Event Flow**:
```python
async def _handle_message(self, topic: str, data: Dict[str, Any]) -> None:
    # Parse topic: exchange, data_type, symbol
    _, data_type, symbol = parse_topic(topic)
    
    if "book" in data_type:
        # Update views first
        self._price_view.update_from_book(data)
        self._history_view.add_book(data)
        # Dispatch to strategies
        await self._strategy_host.dispatch_book(data)
    
    elif "trades" in data_type:
        # Enrich with microstructure features
        enriched = preprocess_trade(dict(data), state=st)
        self._history_view.add_trade(enriched)
        await self._strategy_host.dispatch_trade(enriched)
```

### 3. Strategy Analysis

**Component**: `AdaptiveLeadLagStrategy`  
**File**: `sub/arkpad-ahab2/leadlag/strategy/adaptive_arbitrage.py`

**Event Handlers**:

**on_trade(msg)**:
- Updates online Hayashi-Yoshida estimator buffers
- Leader jump detection (Lee-Mykland test)
- Co-jump filtering (systematic vs idiosyncratic)
- HY scan for affected pairs
- Per-lagger decision aggregation
- Entry gating and executor creation

**on_book(msg)**:
- Updates HY feature buffers (mid, imbalance, OFI)
- ATR bar building and computation
- Tracks last mid for jump amplitude estimation

**Decision Logic**:
```python
# Aggregate multiple feature signals per lagger
decision = self._aggregate_lagger_decision(pair, rlist, jr, leader_jump_bps)

# Gate checks
if decision["strength"] < self.config.entry_threshold:
    return  # Below threshold
    
if not self._gating_allows_entry(lagger, ts_ns):
    return  # Hazard window, active executor, or existing position

# Execute trade
await self._execute_trade(pair, direction, strength, tau_s, decision_meta)
```

### 4. Risk Validation

**Component**: `OrderManager` + `RiskEngine`  
**Files**: `trading_core/runtime/order_manager_impl.py`, `trading_core/runtime/factory.py`

**OrderManager.submit_order() Pipeline**:

```python
async def submit_order(self, order: ProposedOrder) -> OrderSubmissionResult:
    # 1. Risk Engine Evaluation
    decision = await self._risk.evaluate(order)
    if decision.action == RiskAction.reject:
        return OrderSubmissionResult(is_accepted=False, reason="Risk violation")
    
    if decision.action == RiskAction.modify:
        order.px = decision.px  # Modify price if needed
        order.sz = decision.sz  # Modify size if needed
    
    # 2. Balance Check
    ok, reason = self._has_sufficient_balance(order)
    if not ok:
        return OrderSubmissionResult(is_accepted=False, reason=reason)
    
    # 3. Reserve IM/Fees (Perps only)
    if is_perp:
        im_usd = compute_im(...)
        if not self._account.reserve_im(symbol, im_usd, settle=stl):
            return OrderSubmissionResult(is_accepted=False, reason="Insufficient IM")
    
    # 4. Lock Funds (Spot only; perps skip this step)
    if not is_perp:
        if not self._account.lock_funds(order):
            return OrderSubmissionResult(is_accepted=False, reason="Failed to lock funds")
    
    # 5. Convert and Send
    execution_order = self._convert_to_execution_order(order)
    await self._execution.send(execution_order)
    
    return OrderSubmissionResult(is_accepted=True, order_id=order.cl_id)
```

**RiskEngine Rules** (built in factory):
- `StaleDataRule`: Reject orders on stale market data
- `PriceBandRule`: Price must be within ±N bps of mid
- `MaxNotionalRule`: Per-order notional cap
- `MaxOpenOrdersRule`: Concurrent open orders limit
- `InventoryBandsRule`: Symbol-level position limits
- `PortfolioExposureRule`: Total gross USD cap
- `OrderRateRule`: Rate limiting per second

### 5. Order Conversion

**Component**: `exec/bridge.py`  
**File**: `trading_core/exec/bridge.py`

**ProposedOrder → ExecutionOrder Mapping**:

```python
def build_exec_order(po: ProposedOrder) -> ExecutionOrder:
    venue_type = po.venue_type or "cex"
    
    if venue_type == "cex":
        product_type = po.product_type or "spot"  # spot | perpetual
        details = _cex_details(po)  # symbol, side, order_type, size, price, etc.
        venue = po.meta.get("venue") or "unknown"
        
        return ExecutionOrder(
            cl_id=po.cl_id,
            action="place",
            venue_type="cex",
            venue=venue,
            product_type=product_type,
            details=details,
            tags=po.meta.get("tags", {}),
        )
```

**Key Transformations**:
- Symbol normalization (e.g., `BTC-USDT-PERP` → `BTC/USDT:USDT` for perps)
- Enum values extracted (Side.buy → "buy")
- Metadata tags preserved for intent tracking
- Perps parameters (margin_mode, leverage) passed through

### 6. Execution

**Component**: `ExecClient`  
**File**: `trading_core/exec/client.py`

**ZMQ Communication**:
```python
# PUSH orders to engine
async def send(self, order: ExecutionOrder):
    payload = order.model_dump()
    data = json.dumps(payload).encode()
    await self._push.send(data)  # tcp://host:5601

# SUB execution events from engine
async def loop(self):
    async def _on_events(topic: str, payload: dict):
        if topic == "exec.report" and self.on_report:
            report = ExecutionReport.model_validate(payload)
            await self.on_report(report)
        elif topic == "exec.fill" and self.on_fill:
            fill = Fill.model_validate(payload)
            await self.on_fill(fill)
    
    await self._events.loop(_on_events)  # tcp://host:5602
```

**Execution Reports Flow Back**:
1. Engine sends `exec.report` (accepted/rejected/canceled)
2. OrderManager handles report:
   - Update ACK flags
   - Release funds on reject/cancel
   - Persist event to SQLite
3. Engine sends `exec.fill` on partial/full fill
4. OrderManager handles fill:
   - Update account balances
   - Release proportional IM/fees for perps
   - Notify strategy via `IStrategyNotifier`

---

## Lead-Lag Strategy Implementation

### Overview

The `AdaptiveLeadLagStrategy` implements a sophisticated arbitrage strategy based on detecting and exploiting lead-lag relationships between correlated assets using online Hayashi-Yoshida estimation.

**File**: `sub/arkpad-ahab2/leadlag/strategy/adaptive_arbitrage.py` (1517 lines)  
**Config**: `sub/arkpad-ahab2/leadlag/config.py` (325 lines)

### Core Components

#### 1. Online Hayashi-Yoshida Estimator

**Purpose**: Real-time detection of lead-lag relationships and optimal lag estimation

**Features**:
- Rolling window analysis (configurable lookback)
- Multi-feature support (mid, OFI, imbalance, volatility)
- Lag grid search with exclusion zones
- Correlation and Lead-Lag Strength (LLS) thresholds

**Code Reference**:
```python
# Update from trade
self.hy_online.update_from_trade(symbol, msg)

# Update from book (features: mid, imbalance, OFI)
self.hy_online.update_from_book(symbol, msg)

# Analyze relationship
hy = self.hy_online.analyze(
    leader_symbol,
    lagger_symbol,
    feature="ofi",
    target="log_ret",
    include_debug_vectors=False,
)

# Results: rho_star, lls, optimal_lag_seconds, passes_thresholds
```

#### 2. Jump Detection (Lee-Mykland)

**Purpose**: Identify significant price jumps in leader symbols to trigger analysis

**Algorithm**:
- Event-time statistics (rolling window K trades)
- Pre-averaging to reduce noise
- Z-score test with configurable alpha
- Co-jump filtering (systematic vs idiosyncratic moves)

**Code Reference**:
```python
# Initialize
self.jump_detector = AdaptiveJumpDetector(jd_cfg)

# Update on trade
formed = self.jump_detector.update_trade(symbol, msg)
if formed:
    jr = self.jump_detector.detect_jump(symbol)
    if jr.is_jump:
        # Check co-jumps
        co_count, is_systematic = self.jump_detector.detect_co_jumps(symbol, ts_ns)
        if not is_systematic:
            # Trigger HY scan for affected pairs
            affected, results = self._hy_scan_for_leader(symbol)
```

#### 3. ATR-Based Dynamic Brackets

**Purpose**: Adaptive stop-loss and take-profit levels based on market volatility

**Implementation**:
- Fixed-interval mid bars (configurable seconds)
- Wilder ATR calculation
- SL/TP as multiples of ATR
- Floor and cap in bps

**Code Reference**:
```python
# Build bars
bar = self._bar_builders[symbol].on_mid(mid, ts_ns)

# Update ATR
if bar is not None:
    atr_val = self._atr_calcs[symbol].update_from_bar(bar)
    if atr_val > 0:
        self._last_atr_value[symbol] = atr_val

# Use in sizing
sl_bps = (atr_sl_mult * atr_val / price) * 10_000
sl_bps = max(floor_bps, min(cap_bps, sl_bps))
sl_pct = sl_bps / 10_000.0
```

#### 4. Expected Move Predictor (RLS)

**Purpose**: Online learning of expected price moves for fee gating and TP sizing

**Features**:
- Recursive Least Squares with forgetting factor
- Features: [1.0, leader_jump_bps, |rho*|, interaction]
- Target: realized move in bps (τ* seconds later)
- Warm-up blending with artifact baselines

**Code Reference**:
```python
# Predict
x = [1.0, leader_jump_bps, abs(rho_star), leader_jump_bps * abs(rho_star)]
exp_pred = self._expmove.predict(x)

# Blend with artifact during warmup
if self._expmove.ready:
    exp_used = max(exp_pred, blend_k * artifact_bps)
else:
    exp_used = artifact_bps

# Update after position closes
self._expmove.update(x, realized_bps)
```

### Strategy Event Flow

#### on_trade(msg)

```
1. Update HY buffers (trade ticks)
2. If symbol is leader:
   a. Update jump detector
   b. Detect jump (Lee-Mykland test)
   c. Filter co-jumps (systematic filter)
   d. HY scan for affected pairs
   e. Aggregate per-lagger decision
   f. Gate checks (capacity, hazard, position)
   g. Create PositionExecutor
```

#### on_book(msg)

```
1. Update HY buffers (mid, imbalance, OFI)
2. Build ATR bars and update ATR values
3. Track last mid for jump amplitude
```

#### on_fill(fill)

```
1. Route to ExecutorGroup
2. Update metrics (filled entry intents)
3. Prune flat executors
4. Log realized PnL per intent
```

#### on_timer(now_ns)

```
1. Enforce time-limit exits (ExecutorGroup.step)
2. Process pending off-loop decisions (if enabled)
3. Train expected-move predictor on due samples
4. Cleanup expired hazard windows
5. Reload artifacts (if auto-reload enabled)
6. Periodic status logging
```

### Decision Aggregation

**Simple Mode**:
- Select top feature by `w * |rho*|`
- Direction from sign of `rho*`
- Strength = `w * |rho*| * expected_move_bps`

**Sum Mode** (default):
- Aggregate all passing features
- Weighted sum: `Σ(w_i * |rho*_i|)`
- Confidence boost from correlation strength
- Expected move from predictor or artifact

**Code Reference**:
```python
def _aggregate_lagger_decision(self, pair, rlist, jr, leader_jump_bps):
    if simple_mode:
        # Top feature only
        top_feat = max(rlist, key=lambda r: weight(r) * abs(r["rho_star"]))
        direction = sign(top_feat["rho_star"])
        strength = weight(top_feat) * abs(top_feat["rho_star"]) * exp_move_bps
    else:
        # Weighted sum
        wsum = sum(w_i * abs(rho_i) for each feature)
        direction = majority_sign(rlist)
        strength = wsum * exp_move_bps * conf_boost
    
    return {"direction": direction, "strength": strength, "tau_s": tau_star, ...}
```

### Entry Execution

**Sizing**:
```python
multiplier = clamp(signal_strength / entry_threshold, 0, max_signal_multiplier)
amount = (base_position_size_usd * multiplier) / price
```

**Order Construction**:
- Entry: Market + IOC (or banded limit+IOC for protection)
- SL: ATR-derived or static bps
- TP: ATR-derived or expected_move_bps
- TTL: Fixed time_limit_seconds or τ*-based
- Tags: intent_id, role, strategy, symbol_strategy

**Fee Gating** (optional):
```python
gross_bps = signal_strength
entry_fee = taker_bps if is_taker else maker_bps
exit_fee = tp_hit_prob * maker_bps + (1 - tp_hit_prob) * taker_bps
net_bps = gross_bps - (entry_fee + exit_fee + slippage + buffer)

if net_bps <= 0:
    skip_trade()  # Not profitable after costs
```

### Gating Mechanisms

1. **Capacity**: `len(exec_group) < max_concurrent_positions`
2. **Hazard Window**: Cooldown per lagger after entry (seconds)
3. **Active Executor**: Skip if executor already exists (unless stacking allowed)
4. **Position Materiality**: Treat dust positions as flat (epsilon + notional thresholds)
5. **Entry Threshold**: `signal_strength >= entry_threshold`
6. **ATR Availability**: Skip if ATR not yet computed

---

## Configuration Guide

### YAML Configuration Structure

Trading_core uses a unified YAML configuration with environment profiles and variable substitution.

**Example Files**:
- Live (paper): `sub/arkpad-ahab2/examples/leadlag_adaptive_live.yaml`
- Backtest: `sub/arkpad-ahab2/examples/leadlag_adaptive_backtest.yaml`

### Top-Level Sections

```yaml
mode: live | backtest
symbol_rules_path: path/to/symbol_rules.csv
runtime:
  account_id: string
  db_path: ./data/trading.db
  order_manager:
    ack_timeout_ms:
      ioc: 3000    # IOC/market orders
      gtc: 15000   # GTC/posted orders

market_data:
  provider: zmq | postgresql
  # ... provider-specific config

execution:
  provider: zmq
  mode: paper | live
  # ... execution config

strategies:
  - name: strategy-name
    type: module.path.StrategyClass
    config: { ... }

risk:
  global: { ... }
  portfolio: { ... }
  perps: { ... }

observability:
  logging: { ... }
  health_server: { ... }

backtest:  # Only for mode: backtest
  # ... backtest config
```

### Market Data Configuration

#### Live (ZMQ)

```yaml
market_data:
  provider: zmq
  host: 127.0.0.1
  port: 5556                     # Trades socket
  books_port: 5557               # Books socket (optional split publisher)
  exchanges: [BINANCE_FUTURES]   # Topic prefix filter
  deltas:                        # Optional L2 delta/checkpoint support
    enabled: true
    lob_engine: cpp              # cpp | python
    lob_top_n: 10
```

**Topic Filters** (auto-generated from strategy symbols):
- `{EXCHANGE}-preprocessed_trades-{SYMBOL}`
- `{EXCHANGE}-preprocessed_book-{SYMBOL}`
- `{EXCHANGE}-preprocessed_book_delta-{SYMBOL}` (if deltas.enabled)
- `{EXCHANGE}-preprocessed_book_ckpt-{SYMBOL}` (if deltas.enabled)

#### Backtest (PostgreSQL)

```yaml
market_data:
  provider: postgresql  # Auto-set when mode=backtest

backtest:
  start_date: ${BACKTEST_START:2025-09-07T08:00:00Z}
  end_date: ${BACKTEST_END:2025-09-08T08:00:00Z}
  symbols: [BTC-USDT-PERP, ETH-USDT-PERP]
  speed_multiplier: 1000
  microstructure_depth_levels: 10
  
  data_source:
    type: postgresql
    connection: postgresql://user:pass@host:5432/marketdata
    trade_table: trades
    book_table: ob_snapshots
    batch_size: 20000
  
  simulation:
    latency_ms: 5
    slippage_bps: 1
    slippage_mode: bps_only | book_vwap
    execution_pricing: book_vwap | mid
    fee_mode: quote | base
    maker_fee_bps: 10.0
    taker_fee_bps: 10.0
    symbol_rules_path: tools/venue_rules/bybit_spot.csv
    queue_model: queue | instant
    fill_probability: 0.90
    partial_fill_probability: 0.25
    min_fill_ratio: 0.10
    queue_ahead_model: linear | power_law
    adverse_selection_factor: 0.15
```

### Execution Configuration

```yaml
execution:
  provider: zmq
  mode: paper | live
  host: 127.0.0.1
  orders_port: 5601    # PUSH orders to engine
  events_port: 5602    # SUB exec.report/fill from engine

# Paper mode allows initial balances
initial_balance:  # Only in paper mode
  USDT: 100000
  BTC: 1.0
```

### Risk Configuration

```yaml
risk:
  global:
    px_band_bp: 100                    # Price band ±1% from mid
    max_notional_per_order: 10000      # USD per order
    max_ord_rate_per_sec: 10           # Rate limit
    max_open_orders: 100               # Total open orders
    reject_on_stale_data_ms: 500       # Stale data threshold
  
  portfolio:
    max_gross_usd: 60000               # Total gross exposure
    inventory_bands:                   # Per-symbol position limits
      BTC-USDT-PERP: {min: -1.0, max: 1.0}
  
  perps:
    max_leverage: 10
    max_order_notional_usd: 100000
    maintenance_buffer_bps: 50
    taker_bps: 5.0                     # Fee assumptions for IM calc
    maker_bps: 2.5
  
  profiles:
    leadlag: {}                        # Strategy-specific overrides
```

### Lead-Lag Strategy Configuration

```yaml
strategies:
  - name: adaptive-leadlag
    type: leadlag.strategy.adaptive_arbitrage.AdaptiveLeadLagStrategy
    config:
      # Universe & Artifacts
      market_type: perp                # spot | perp (normalized)
      artifacts_path: ${ART_DIR:leadlag/artifacts/latest}
      artifacts_auto_reload: true
      artifacts_refresh_interval_seconds: 600
      symbols: [BTC-USDT-PERP, ETH-USDT-PERP]  # Optional explicit list
      
      # Online HY Analysis
      inference_lookback_seconds: 240
      correlation_threshold: 0.4       # Min |rho*| at optimal lag
      lls_threshold: 1.0               # Min LLS for valid relationship
      
      # Entry/Exit Logic
      entry_threshold: 1.0             # Gate: strength >= threshold
      scoring_mode: sum                # sum | max
      simple_mode: false               # true = top feature only
      
      # Position Sizing
      base_position_size_usd: 500.0
      max_signal_multiplier: 1.0       # Size cap: base * min(signal/threshold, max)
      
      # ATR Dynamic Brackets
      atr_enabled: true
      atr_bar_seconds: 15
      atr_period: 40
      atr_sl_mult: 6                   # SL = 6 * ATR
      atr_tp_mult: 8                   # TP = 8 * ATR
      atr_floor_bps: 10.0              # Min bracket distance
      atr_cap_bps: 120.0               # Max bracket distance
      atr_enable_sl: true
      atr_enable_tp: true
      
      # Static Brackets (used if ATR disabled or as fallback)
      stop_loss_bps: 0.0               # Set to 0 when ATR enabled
      take_profit_bps: null            # null = use expected_move_bps
      
      # Execution
      time_limit_seconds: 1200         # Position TTL; 0 disables
      entry_order_type: market
      entry_time_in_force: ioc
      entry_banded_ioc_enable: true    # Convert market to limit+IOC with band
      entry_ioc_price_band_bps: 15.0   # Protection band
      
      # Fee-Aware Gating
      enable_fee_gate: true
      taker_fee_bps: 5.0
      maker_fee_bps: 2.5
      tp_hit_probability: 0.45         # Probability TP hits vs SL/TTL
      expected_slippage_bps_total: 2.0
      arb_gate_buffer_bps: 3.0
      
      # Risk Limits
      max_concurrent_positions: 12
      max_total_notional_usd: 60000
      max_single_position_usd: 10000
      hazard_window_seconds: 12        # Cooldown per lagger
      allow_stacking: false
      
      # Perps Execution Passthrough
      perp_margin_mode: cross          # cross | isolated
      perp_leverage: 10
      
      # Component Configs (advanced)
      hy_estimator_config:
        lag_min_s: -12.0
        lag_max_s: 12.0
        lag_step_s: 0.05
        exclude_low_s: -0.3
        exclude_high_s: 0.3
      
      jump_detection_config:
        window_K: 50
        alpha: 0.0001
        co_jump_threshold: 3
      
      # Expected Move Predictor (RLS)
      expmove_enabled: true
      expmove_lambda: 0.99             # Forgetting factor
      expmove_ridge: 0.001
      expmove_min_samples: 100
      expmove_cap_bps: 50.0
      expmove_blend_k: 0.7
      
      # Diagnostics
      decision_scores_csv_path: ./logs/decision_scores.csv
```

### Environment Variable Substitution

```yaml
# Use ${VAR:default} syntax
artifacts_path: ${ART_DIR:leadlag/artifacts/latest}
start_date: ${BACKTEST_START:2025-09-07T08:00:00Z}

# Set via shell
export ART_DIR=/path/to/custom/artifacts
export BACKTEST_START=2025-10-01T00:00:00Z
```

### Observability Configuration

```yaml
observability:
  logging:
    level: INFO
    structured: false              # true = JSON logging
    file: ./logs/trading.log       # Optional file output
  
  health_server:
    enabled: true
    host: 127.0.0.1
    port: 8082
  
  accounts_zmq:                    # Account snapshot sync
    host: 127.0.0.1
    port: 5562
  
  metrics_aggregator:
    enabled: true
    poll_interval_sec: 1.0
    log_interval_sec: 120.0
```

---

## Deployment Instructions

### Prerequisites

1. **Build C++ Engine** (per user rules, use bash):
   ```bash
   cd /home/tensor/latentspeed
   ./run.sh --release
   ```

2. **Python Environment**:
   ```bash
   # Create virtual environment (if not exists)
   python3 -m venv .venv
   source .venv/bin/activate
   
   # Install trading_core
   cd sub/arkpad-ahab2
   pip install -e .
   ```

3. **Database Setup** (for backtests):
   ```bash
   # Install TimescaleDB extension
   psql -U postgres -d marketdata -c "CREATE EXTENSION IF NOT EXISTS timescaledb;"
   
   # Create tables (see trading_core/data/schema.sql)
   ```

### Live Deployment (Paper Mode)

#### Step 1: Start Marketstream

```bash
# Terminal 1
cd /home/tensor/latentspeed
./build/linux-release/marketstream configs/config.yml
```

**Verify**:
- Trades publishing on tcp://127.0.0.1:5556
- Books publishing on tcp://127.0.0.1:5557
- Check logs for symbol subscriptions

#### Step 2: Start Trading Engine

```bash
# Terminal 2
cd /home/tensor/latentspeed
./build/linux-release/trading_engine_service \
  --exchange bybit \
  --api-key YOUR_API_KEY \
  --api-secret YOUR_API_SECRET \
  --mode paper \
  --orders-port 5601 \
  --events-port 5602
```

**Verify**:
- Orders socket listening on tcp://127.0.0.1:5601
- Events socket publishing on tcp://127.0.0.1:5602

#### Step 3: Start Trading Core

```bash
# Terminal 3
cd /home/tensor/latentspeed/sub/arkpad-ahab2
python -m trading_core.runtime.launcher \
  examples/leadlag_adaptive_live.yaml \
  --profile development \
  --log-level INFO
```

**Verify**:
- Strategy initialized with artifacts loaded
- ZMQ subscriptions established
- Health server accessible: `curl http://127.0.0.1:8082/health`

#### Step 4: Monitor

```bash
# Logs
tail -f logs/leadlag_live.log

# Metrics endpoint
curl http://127.0.0.1:8082/metrics

# Decision scores (if enabled)
tail -f logs/decision_scores_leadlag_live.csv
```

### Live Deployment (Production Mode)

**Critical Differences**:
1. Set `execution.mode: live` (not paper)
2. Remove `initial_balance` section
3. Configure real API keys via environment variables
4. Enable structured logging: `observability.logging.structured: true`
5. Set proper risk limits aligned with account size

**Launch**:
```bash
export BYBIT_API_KEY="your_key"
export BYBIT_API_SECRET="your_secret"

python -m trading_core.runtime.launcher \
  examples/leadlag_adaptive_live.yaml \
  --profile production \
  --log-level INFO \
  --structured-logging
```

### Backtest Deployment

#### Step 1: Prepare Data

```bash
# Export historical data to PostgreSQL
# (Assumes you have trades/ob_snapshots tables populated)

psql -U db_analyst -d marketdata -c "
  SELECT count(*) FROM trades 
  WHERE symbol IN ('BTC-USDT-PERP', 'ETH-USDT-PERP')
  AND timestamp >= '2025-09-07 08:00:00'
  AND timestamp < '2025-09-08 08:00:00';
"
```

#### Step 2: Configure Backtest

Edit `examples/leadlag_adaptive_backtest.yaml`:
```yaml
backtest:
  start_date: 2025-09-07T08:00:00Z
  end_date: 2025-09-08T08:00:00Z
  symbols: [BTC-USDT-PERP, ETH-USDT-PERP]
  
  data_source:
    connection: postgresql://db_analyst:lagger@127.0.0.1:5432/marketdata
```

#### Step 3: Run Backtest

```bash
cd /home/tensor/latentspeed/sub/arkpad-ahab2

python -m trading_core.runtime.launcher \
  examples/leadlag_adaptive_backtest.yaml \
  --report-json ./results/backtest_report.json \
  --log-level INFO
```

#### Step 4: Analyze Results

**Console Output**:
```
=== Backtest Summary ===
Net Profit: 1234.56
Total Return: 12.35%
Max Drawdown: -3.45%
Sharpe Ratio: 2.15
Total Trades: 89
```

**JSON Report**:
```bash
cat results/backtest_report.json | jq .

{
  "net_profit": 1234.56,
  "total_return_percent": 12.35,
  "max_drawdown": -3.45,
  "sharpe_ratio": 2.15,
  "total_trades": 89,
  "win_rate": 0.67,
  "average_trade_pnl": 13.87
}
```

### Docker Deployment (Optional)

```dockerfile
# Dockerfile for trading_core
FROM python:3.11-slim

WORKDIR /app
COPY sub/arkpad-ahab2 /app/trading_core
COPY requirements.txt /app/

RUN pip install --no-cache-dir -r requirements.txt
RUN pip install -e /app/trading_core

ENTRYPOINT ["python", "-m", "trading_core.runtime.launcher"]
CMD ["config.yaml"]
```

**Docker Compose**:
```yaml
version: '3.8'
services:
  marketstream:
    image: latentspeed/marketstream:latest
    command: ["configs/config.yml"]
    ports:
      - "5556:5556"
      - "5557:5557"
  
  trading_engine:
    image: latentspeed/trading_engine:latest
    environment:
      - BYBIT_API_KEY=${BYBIT_API_KEY}
      - BYBIT_API_SECRET=${BYBIT_API_SECRET}
    ports:
      - "5601:5601"
      - "5602:5602"
    depends_on:
      - marketstream
  
  trading_core:
    build: .
    volumes:
      - ./examples:/app/examples
      - ./logs:/app/logs
      - ./data:/app/data
    command: ["examples/leadlag_adaptive_live.yaml"]
    depends_on:
      - trading_engine
```

### Health Checks

**Trading Core Health Endpoint**:
```bash
curl http://127.0.0.1:8082/health
```

**Response**:
```json
{
  "status": "healthy",
  "subsystems": {
    "market_data": "ready",
    "execution": "ready",
    "strategies": "ready"
  },
  "timestamp": "2025-10-28T14:00:00Z"
}
```

**Strategy Metrics**:
```bash
curl http://127.0.0.1:8082/metrics
```

**Response**:
```json
{
  "active_positions": 3,
  "filled_trades": 15,
  "errors": 0,
  "hy_passes_today": 42
}
```

### Graceful Shutdown

**Signal Handling**:
```bash
# Send SIGTERM for graceful shutdown
kill -TERM <pid>

# Or use SIGINT (Ctrl+C)
```

**Shutdown Process**:
1. Strategy stops accepting new signals
2. Active executors close positions (time-limit or SL/TP)
3. OrderManager cancels pending orders
4. MarketDataGateway stops subscriptions
5. Persistence stores flush to disk
6. Exit with status 0

---

## Code References

### Core Runtime Components

| Component | File Path | Key Classes/Functions |
|-----------|-----------|----------------------|
| **Runtime Orchestration** | `trading_core/runtime/core.py` | `CleanTradingRuntime`, `IManagedService` |
| **Factory** | `trading_core/runtime/factory.py` | `LiveRuntimeFactory`, `BacktestRuntimeFactory`, `RuntimeComponentsBuilder` |
| **Market Data Gateway** | `trading_core/runtime/market_data_gateway.py` | `MarketDataGateway._handle_message` |
| **Strategy Context** | `trading_core/runtime/strategy_context_impl.py` | `CleanStrategyContext` |
| **Order Manager** | `trading_core/runtime/order_manager_impl.py` | `OrderManager.submit_order`, `handle_fill` |
| **Execution Bridge** | `trading_core/exec/bridge.py` | `build_exec_order`, `_cex_details` |
| **Execution Client** | `trading_core/exec/client.py` | `ExecClient.send`, `ExecClient.loop` |
| **Launcher** | `trading_core/runtime/launcher.py` | `main()`, `create_clean_runtime_from_config_file` |

### Interfaces (Clean Architecture)

| Interface | File Path | Purpose |
|-----------|-----------|---------|
| **IStrategyContext** | `trading_core/interfaces/strategy.py` | Strategy API contract |
| **IOrderManager** | `trading_core/interfaces/order_manager.py` | Order submission interface |
| **ExecutionClientInterface** | `trading_core/interfaces/execution.py` | Execution client contract |
| **MarketDataSource** | `trading_core/interfaces/market_data.py` | Market data provider contract |
| **IRiskManager** | `trading_core/interfaces/risk.py` | Risk engine interface |

### Lead-Lag Strategy

| Component | File Path | Line Range | Description |
|-----------|-----------|------------|-------------|
| **Strategy** | `sub/arkpad-ahab2/leadlag/strategy/adaptive_arbitrage.py` | 1-1517 | Complete strategy implementation |
| **Config Schema** | `sub/arkpad-ahab2/leadlag/config.py` | 1-325 | Pydantic config model |
| **HY Estimator** | `sub/arkpad-ahab2/leadlag/utils/online_hy_estimator.py` | - | Online Hayashi-Yoshida |
| **Jump Detector** | `sub/arkpad-ahab2/leadlag/utils/jump_detector.py` | - | Lee-Mykland jump test |
| **ATR Bars** | `sub/arkpad-ahab2/leadlag/utils/bars_atr.py` | - | Fixed-interval bars + Wilder ATR |
| **ExpMove Predictor** | `sub/arkpad-ahab2/leadlag/utils/expmove_predictor.py` | - | RLS expected move predictor |

### Schemas

| Schema | File Path | Models |
|--------|-----------|--------|
| **Order** | `trading_core/schemas/order.py` | `ProposedOrder`, `CancelOrder`, `Side`, `OrderType`, `TIF`, `ProductType` |
| **Execution** | `trading_core/schemas/execution.py` | `ExecutionOrder`, `ExecutionReport`, `Fill` |
| **Symbol Rules** | `trading_core/schemas/symbol_rules.py` | `SymbolRule` (tick size, lot size, limits) |

### Example Configurations

| Config | File Path | Mode |
|--------|-----------|------|
| **Live (Paper)** | `sub/arkpad-ahab2/examples/leadlag_adaptive_live.yaml` | Live ZMQ + paper execution |
| **Backtest** | `sub/arkpad-ahab2/examples/leadlag_adaptive_backtest.yaml` | PostgreSQL replay + simulation |
| **Backtest (Perps)** | `sub/arkpad-ahab2/examples/leadlag_adaptive_backtest_perp.yaml` | Perps-specific backtest |

---

## Troubleshooting

### Common Issues

#### 1. No Market Data Received

**Symptoms**:
- Strategy shows no `on_trade` or `on_book` calls
- PriceView returns None for symbols

**Diagnosis**:
```bash
# Check ZMQ topics
zmq_sub -c tcp://127.0.0.1:5556 -f "" | head -20

# Verify filters
grep "ZMQ market data" logs/trading.log
```

**Solutions**:
- Verify `market_data.exchanges` matches marketstream publisher (exact case)
- Check symbol format alignment (e.g., `BTC-USDT-PERP` vs `BTCUSDT`)
- Ensure `books_port` is set if using split publisher
- Verify marketstream is running and publishing

#### 2. Orders Rejected by Risk

**Symptoms**:
- Logs show "Risk violation" or "Insufficient balance"

**Diagnosis**:
```python
# Check risk decision details
grep "Order .* rejected by risk" logs/trading.log
```

**Solutions**:
- **Stale Data**: Increase `risk.global.reject_on_stale_data_ms` (especially for fast backtests)
- **Price Band**: Widen `risk.global.px_band_bp` if volatility is high
- **Notional**: Increase `risk.global.max_notional_per_order`
- **Balance**: Verify account balances are sufficient (check `initial_balance` for paper mode)
- **Perps IM**: Ensure symbol rules are loaded and max_leverage is set

#### 3. Orders Not Executed

**Symptoms**:
- OrderManager logs "sent to execution client" but no fills

**Diagnosis**:
```bash
# Check execution reports
grep "exec.report" logs/trading.log

# Monitor engine logs
tail -f /var/log/trading_engine_service.log
```

**Solutions**:
- Verify trading_engine_service is running
- Check ZMQ ports match (`orders_port: 5601`, `events_port: 5602`)
- Verify venue connectivity (API keys, network)
- Check symbol normalization (e.g., `BTC/USDT:USDT` for perps)
- Review engine logs for venue rejections

#### 4. Lead-Lag Strategy Not Trading

**Symptoms**:
- No "jump" or "hy_pass" logs
- Active positions always 0

**Diagnosis**:
```bash
# Check jump detection
grep "jump=" logs/trading.log

# Check HY scans
grep "hy_pass" logs/trading.log

# Check ATR warmup
grep "atr status" logs/trading.log
```

**Solutions**:
- **No Jumps Detected**: Lower `jump_detection_config.alpha` threshold
- **No HY Passes**: Lower `correlation_threshold` or `lls_threshold`
- **ATR Not Ready**: Wait for ATR warmup (period bars required)
- **Artifacts Missing**: Verify `artifacts_path` exists and contains `top_pairs.json`, `top_features.json`, `baselines.json`
- **Entry Threshold**: Lower `entry_threshold` for more aggressive entries
- **Capacity Full**: Check `max_concurrent_positions` vs active executor count

#### 5. Backtest Runs Slow

**Symptoms**:
- Very low speed_multiplier effective rate
- High memory usage

**Solutions**:
- Reduce `backtest.symbols` to fewer symbols
- Increase `data_source.batch_size` (e.g., 50000)
- Use `execution_pricing: mid` instead of `book_vwap`
- Disable queue modeling: `queue_model: instant`
- Reduce microstructure depth: `microstructure_depth_levels: 5`

#### 6. Perps Orders Rejected

**Symptoms**:
- "Insufficient IM" or "reduce_only violation"

**Diagnosis**:
```bash
# Check IM reservation logs
grep "reserve_im" logs/trading.log

# Check position state
grep "position_check" logs/trading.log
```

**Solutions**:
- Verify `symbol_rules_path` is loaded for perps
- Check `risk.perps.max_leverage` is set
- Ensure index price getter is wired (factory sets this automatically)
- For reduce-only: verify existing position exists and is non-zero
- Check margin mode matches venue (`perp_margin_mode: cross`)

### Debug Logging

**Enable Debug Level**:
```bash
python -m trading_core.runtime.launcher config.yaml --log-level DEBUG
```

**Key Debug Patterns**:
```bash
# Market data flow
grep "on_trade: symbol=" logs/trading.log

# Risk decisions
grep "Risk validation" logs/trading.log

# Order lifecycle
grep "Order .* sent to execution" logs/trading.log
grep "Received execution report" logs/trading.log

# Strategy decisions
grep "entry_open" logs/trading.log
grep "hy_pass" logs/trading.log
```

### Performance Monitoring

**Metrics to Track**:
- Active positions: `curl http://127.0.0.1:8082/metrics | jq .active_positions`
- Fill rate: Compare `filled_trades` vs `orders_submitted`
- Error count: Monitor `errors` field
- HY pass rate: `hy_passes_today` vs total jumps detected

---

## Recommendations

### Production Readiness

#### 1. Monitoring and Alerting

**Implement**:
- **Prometheus/Grafana**: Export metrics from health server
- **Alerts**: PagerDuty/Slack for critical errors
  - Market data disconnection > 30s
  - Fill rate < 50% over 1 hour
  - Account balance drops > 20%
  - Error rate > 10 per minute
- **Logging**: Structured JSON logs to Elasticsearch/Loki

**Example Alert**:
```yaml
# Prometheus alert rule
- alert: MarketDataDisconnected
  expr: trading_core_market_data_ready == 0
  for: 30s
  labels:
    severity: critical
  annotations:
    summary: "Market data disconnected"
```

#### 2. Risk Management Enhancements

**Implement**:
- **Circuit Breaker**: Auto-halt on drawdown > threshold
- **Position Limits**: Per-symbol max notional
- **Correlation Limits**: Max exposure to correlated pairs
- **Volatility Scaling**: Adjust sizing based on realized vol
- **Time-of-Day Gates**: Avoid low-liquidity periods

**Example**:
```python
# In strategy or risk engine
if daily_drawdown > 0.05:  # 5% daily DD
    self.circuit_breaker_active = True
    cancel_all_orders()
    close_all_positions()
```

#### 3. Artifact Pipeline

**Implement**:
- **Daily Scanner**: Cron job to update top_pairs/features/baselines
- **Validation**: Backtest new artifacts before deploying
- **Versioning**: Tag artifacts with date/version
- **Rollback**: Keep N previous artifact sets

**Workflow**:
```bash
# Daily 00:00 UTC
0 0 * * * /home/tensor/latentspeed/scripts/scan_leadlag.sh

# scan_leadlag.sh
python -m leadlag.scanner.daily_scanner \
  --start-date yesterday \
  --end-date today \
  --output leadlag/artifacts/$(date +%Y%m%d)

# Symlink to latest
ln -sf $(date +%Y%m%d) leadlag/artifacts/latest
```

#### 4. Data Quality Checks

**Implement**:
- **Staleness Monitoring**: Alert if book age > 1s
- **Sequence Gap Detection**: Count and log missing sequences
- **Duplicate Detection**: Track and filter duplicate messages
- **Outlier Filtering**: Reject prices outside N-sigma bands

**Example**:
```python
# In MarketDataGateway
if next_seq != prev_seq + 1:
    inc_seq_gap(symbol)
    logger.warning(f"Sequence gap: {symbol} {prev_seq} -> {next_seq}")
```

#### 5. Testing Strategy

**Implement**:
- **Unit Tests**: Core logic (HY estimator, jump detector, decision aggregation)
- **Integration Tests**: End-to-end with mock data
- **Backtest Sweep**: Parameter grid search
- **Paper Trading**: 1-2 weeks before live
- **Shadow Mode**: Run alongside live without execution

**Example**:
```bash
# Run unit tests
pytest tests/unit/leadlag/

# Run integration test
pytest tests/integration/test_leadlag_end_to_end.py

# Backtest sweep
python scripts/sweep_leadlag_params.py \
  --param entry_threshold --range 0.5,2.0,0.1 \
  --param atr_sl_mult --range 2,8,1
```

#### 6. Documentation and Runbooks

**Create**:
- **Deployment Checklist**: Pre-launch verification steps
- **Incident Response**: Runbook for common failures
- **Parameter Tuning Guide**: How to adjust thresholds
- **Architecture Diagram**: Visual flow with port numbers
- **API Reference**: Strategy methods and context interface

#### 7. Performance Optimization

**Considerations**:
- **C++ Port**: Migrate hot paths (HY estimator) to C++ if needed
- **Message Batching**: Batch fills/reports for bulk processing
- **Memory Pools**: Pre-allocate buffers for market data
- **Lock-Free Queues**: Replace Python queues with lock-free structures
- **Profiling**: Use `py-spy` to identify bottlenecks

**Example**:
```bash
# Profile live session
py-spy record -o profile.svg -- python -m trading_core.runtime.launcher config.yaml
```

### Future Enhancements

1. **Multi-Leg Execution**: Simultaneous leader + lagger trades
2. **Adaptive Thresholds**: Dynamic entry_threshold based on market regime
3. **ML Integration**: Use gradient boosting for expected move prediction
4. **Cross-Venue Arbitrage**: Execute legs on different exchanges
5. **MEV Protection**: Bundle trades on-chain (DEX integration)
6. **Smart Order Routing**: Split across venues for best execution

---

## Conclusion

This report provides a comprehensive guide to the lead-lag strategy implementation within the trading_core architecture. The system demonstrates clean architectural principles with complete separation between market data ingestion, strategy logic, risk management, and execution.

**Key Takeaways**:
1. **Modular Design**: Each component is independently testable and replaceable
2. **Live/Backtest Parity**: Same strategy code runs in simulation and production
3. **Robust Risk Management**: Multi-layered validation before order execution
4. **Production-Ready**: Health checks, metrics, structured logging, and graceful shutdown
5. **Extensible**: Easy to add new strategies, risk rules, or execution venues

**Next Steps**:
1. Deploy to paper trading and validate end-to-end flow
2. Run backtests over extended periods (1-3 months)
3. Tune parameters based on backtest results
4. Implement monitoring and alerting
5. Gradually scale position sizes in production

For questions or issues, refer to the troubleshooting section or consult the inline code documentation.

---

**Report Generated**: 2025-10-28  
**Version**: 1.0  
**Maintained By**: Trading Infrastructure Team

