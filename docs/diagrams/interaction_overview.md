# Interaction Overview Diagram - Trading Engine Service

This diagram shows the interaction between components during order processing, combining control flow with detailed sequence interactions.

```mermaid
sequenceDiagram
    participant Client as External Client<br/>(Strategy/Algorithm)
    participant ZMQ_Pull as ZMQ PULL Socket<br/>:5601
    participant RecvThread as Order Receiver Thread<br/>(Core 2, RT)
    participant Parser as Order Parser<br/>(parse_execution_order_hft)
    participant Pool as Memory Pools<br/>(Orders/Reports/Fills)
    participant Processor as Order Processor<br/>(process_execution_order_hft)
    participant Dispatcher as Action Dispatcher<br/>(FNV-1a Hash)
    participant Router as Venue Router<br/>(VenueRouter)
    participant Adapter as Exchange Adapter<br/>(IExchangeAdapter)
    participant Connector as Connector Framework<br/>(HyperliquidPerpetualConnector)
    participant Tracker as Order Tracker<br/>(ClientOrderTracker)
    participant RestAPI as REST API Client<br/>(Boost.Beast HTTPS)
    participant Exchange as Exchange<br/>(Hyperliquid API)
    participant WSStream as WebSocket User Stream<br/>(Async Updates)
    participant Serializer as JSON Serializer<br/>(RapidJSON)
    participant PubQueue as Publish Queue<br/>(LockFreeSPSCQueue)
    participant PubThread as Publisher Thread<br/>(Core 3, RT)
    participant ZMQ_Pub as ZMQ PUB Socket<br/>:5602
    participant Subscriber as ZMQ Subscribers<br/>(Reports/Fills)

    Note over Client,Subscriber: === ORDER PLACEMENT FLOW ===
    
    Client->>ZMQ_Pull: Send JSON Order Message
    activate ZMQ_Pull
    
    loop Non-blocking Receive Loop
        RecvThread->>ZMQ_Pull: zmq_recv(DONTWAIT)
        ZMQ_Pull-->>RecvThread: Message or EAGAIN
    end
    
    activate RecvThread
    RecvThread->>Parser: parse_execution_order_hft(string_view)
    activate Parser
    
    Parser->>Pool: Allocate HFTExecutionOrder
    activate Pool
    Pool-->>Parser: Order Object (or nullptr)
    deactivate Pool
    
    alt Pool Exhausted
        Parser->>RecvThread: nullptr
        RecvThread->>RecvThread: Log Error, Increment Stats
        RecvThread-->>ZMQ_Pull: Continue Loop
    else Pool Available
        Parser->>Parser: RapidJSON Parse (Zero-copy)
        Parser->>Parser: Fill Order Fields (Fixed Strings)
        Parser->>Parser: Copy Tags/Params to FlatMap
        Parser-->>RecvThread: HFTExecutionOrder*
        deactivate Parser
        
        RecvThread->>Processor: process_execution_order_hft(order)
        activate Processor
        
        Processor->>Dispatcher: Hash action string (FNV-1a)
        activate Dispatcher
        Dispatcher-->>Processor: ActionKind (Place/Cancel/Replace)
        deactivate Dispatcher
        
        alt Action: PLACE
            Processor->>Processor: Check Deduplication Map
            alt Already Processed & Pending
                Processor->>RecvThread: Return (Ignore Duplicate)
            else New or Retryable
                Processor->>Processor: Validate Fields (symbol, price, size)
                
                alt Validation Failed
                    Processor->>Pool: Allocate HFTExecutionReport
                    activate Pool
                    Pool-->>Processor: Report Object
                    deactivate Pool
                    Processor->>Processor: Fill Rejection Report
                    Processor->>Serializer: Serialize Report
                    activate Serializer
                    Serializer-->>Processor: JSON String
                    deactivate Serializer
                    Processor->>PubQueue: Push PublishMessage
                    Processor-->>RecvThread: Return
                else Validation Success
                    Processor->>Router: get_adapter(venue_key)
                    activate Router
                    Router-->>Processor: IExchangeAdapter* (or nullptr)
                    deactivate Router
                    
                    alt Adapter Not Found
                        Processor->>Processor: Send Rejection Report
                        Processor-->>RecvThread: Return
                    else Adapter Found
                        Processor->>Processor: Build OrderRequest
                        Processor->>Adapter: place_order(request)
                        activate Adapter
                        
                        Adapter->>Connector: buy/sell(symbol, amount, price, ...)
                        activate Connector
                        
                        Connector->>Tracker: Create InFlightOrder
                        activate Tracker
                        Tracker-->>Connector: InFlightOrder Created
                        deactivate Tracker
                        
                        Connector->>RestAPI: POST /exchange/order
                        activate RestAPI
                        RestAPI->>Exchange: HTTPS Request
                        activate Exchange
                        Exchange-->>RestAPI: Order Response (exchange_order_id)
                        deactivate Exchange
                        RestAPI-->>Connector: Parsed Response
                        deactivate RestAPI
                        
                        alt Exchange Error
                            Connector-->>Adapter: OrderResponse(success=false)
                            Adapter-->>Processor: OrderResponse with reason
                            Processor->>Processor: Send Rejection Report
                            Processor-->>RecvThread: Return
                        else Exchange Success
                            Connector->>Tracker: Update InFlightOrder (exchange_order_id)
                            activate Tracker
                            Tracker-->>Connector: Updated
                            deactivate Tracker
                            Connector-->>Adapter: OrderResponse(success=true)
                            deactivate Connector
                            Adapter-->>Processor: OrderResponse
                            deactivate Adapter
                            
                            Processor->>Processor: Cache in pending_orders_
                            Processor->>Processor: Mark in processed_orders_
                            Processor->>Pool: Allocate HFTExecutionReport
                            activate Pool
                            Pool-->>Processor: Report Object
                            deactivate Pool
                            Processor->>Processor: Fill Acceptance Report
                            Processor->>Serializer: Serialize Report
                            activate Serializer
                            Serializer-->>Processor: JSON String
                            deactivate Serializer
                            Processor->>PubQueue: Push PublishMessage
                            Processor->>Processor: Update HFTStats (latency, count)
                            Processor-->>RecvThread: Return
                        end
                    end
                end
            end
            
        else Action: CANCEL
            Processor->>Processor: Resolve cancel_cl_id_to_cancel
            Processor->>Processor: Lookup in pending_orders_
            
            alt Order Not Found
                Processor->>Processor: Send Rejection Report
                Processor-->>RecvThread: Return
            else Order Found
                Processor->>Router: get_adapter(venue_key)
                activate Router
                Router-->>Processor: IExchangeAdapter*
                deactivate Router
                
                Processor->>Adapter: cancel_order(cancel_request)
                activate Adapter
                Adapter->>Connector: cancel(client_order_id)
                activate Connector
                Connector->>RestAPI: POST /exchange/cancel
                activate RestAPI
                RestAPI->>Exchange: HTTPS Cancel Request
                activate Exchange
                Exchange-->>RestAPI: Cancel Response
                deactivate Exchange
                RestAPI-->>Connector: Response
                deactivate RestAPI
                
                alt Order Not Found at Exchange (Idempotent)
                    Connector-->>Adapter: Response (not found)
                    deactivate Connector
                    Adapter-->>Processor: Success (idempotent)
                    deactivate Adapter
                    Processor->>Processor: Publish Synthetic Canceled Report
                else Cancel Success
                    Connector-->>Adapter: Response (success)
                    deactivate Connector
                    Adapter-->>Processor: Success
                    deactivate Adapter
                    Processor->>Processor: Send Acceptance Report
                end
                Processor-->>RecvThread: Return
            end
            
        else Action: REPLACE
            Processor->>Processor: Resolve replace_cl_id_to_replace
            Processor->>Processor: Lookup in pending_orders_
            Processor->>Processor: Infer new price/size from tags
            Processor->>Router: get_adapter(venue_key)
            activate Router
            Router-->>Processor: IExchangeAdapter*
            deactivate Router
            Processor->>Adapter: modify_order(modify_request)
            activate Adapter
            Adapter->>Connector: Modify order via REST
            Connector-->>Adapter: Response
            deactivate Adapter
            Processor->>Processor: Send Acceptance/Rejection
            Processor-->>RecvThread: Return
        end
        
        deactivate Processor
    end
    
    deactivate RecvThread
    deactivate ZMQ_Pull
    
    Note over Client,Subscriber: === ASYNC UPDATE FLOW (WebSocket User Stream) ===
    
    activate WSStream
    WSStream->>Connector: Order Update Message (WebSocket)
    activate Connector
    Connector->>Tracker: Update InFlightOrder State
    activate Tracker
    Tracker->>Tracker: Apply State Transition
    Tracker-->>Connector: State Updated
    deactivate Tracker
    
    Connector->>Adapter: Trigger on_order_update callback
    activate Adapter
    Adapter->>Processor: on_order_update_hft(OrderUpdate)
    activate Processor
    
    Processor->>Processor: Check if order in pending_orders_
    
    alt Order Unknown (Lazy Rehydration)
        Processor->>Adapter: query_order(symbol, cl_id)
        Adapter->>Connector: Query via REST
        Connector->>RestAPI: GET /exchange/order
        activate RestAPI
        RestAPI->>Exchange: Query Request
        activate Exchange
        Exchange-->>RestAPI: Order Details
        deactivate Exchange
        RestAPI-->>Connector: Response
        deactivate RestAPI
        Connector-->>Adapter: OrderUpdate
        Adapter-->>Processor: OrderUpdate (full details)
        Processor->>Processor: Insert to pending_orders_
    end
    
    Processor->>Processor: Normalize Status (utils::normalize_report_status)
    Processor->>Processor: Map Reason Code (DefaultReasonMapper)
    Processor->>Processor: Ensure 'venue' tag
    
    Processor->>Pool: Allocate HFTExecutionReport
    activate Pool
    Pool-->>Processor: Report Object
    deactivate Pool
    
    Processor->>Serializer: Serialize Report
    activate Serializer
    Serializer-->>Processor: JSON String
    deactivate Serializer
    
    Processor->>PubQueue: Push PublishMessage
    
    alt Terminal Status (filled/canceled/rejected)
        Processor->>Processor: Remove from pending_orders_
    end
    
    Processor->>Processor: Update HFTStats
    Processor-->>Adapter: Callback Complete
    deactivate Processor
    deactivate Adapter
    deactivate Connector
    deactivate WSStream
    
    Note over Client,Subscriber: === FILL EVENT FLOW ===
    
    activate WSStream
    WSStream->>Connector: Fill/Trade Message (WebSocket)
    activate Connector
    Connector->>Tracker: Record Trade Update
    activate Tracker
    Tracker-->>Connector: Trade Recorded
    deactivate Tracker
    
    Connector->>Adapter: Trigger on_fill callback
    activate Adapter
    Adapter->>Processor: on_fill_hft(FillData)
    activate Processor
    
    Processor->>Processor: Parse Fill Fields (price, size, fee)
    Processor->>Processor: Copy Tags from Order
    Processor->>Processor: Normalize Symbol (ISymbolMapper)
    Processor->>Processor: Tag execution_type (live/external)
    
    Processor->>Pool: Allocate HFTFill
    activate Pool
    Pool-->>Processor: Fill Object
    deactivate Pool
    
    Processor->>Serializer: Serialize Fill
    activate Serializer
    Serializer-->>Processor: JSON String
    deactivate Serializer
    
    Processor->>PubQueue: Push PublishMessage
    Processor->>Processor: Update HFTStats
    Processor-->>Adapter: Callback Complete
    deactivate Processor
    deactivate Adapter
    deactivate Connector
    deactivate WSStream
    
    Note over Client,Subscriber: === PUBLISHING FLOW ===
    
    loop Publisher Thread Loop
        activate PubThread
        PubThread->>PubQueue: Pop Message (Non-blocking)
        activate PubQueue
        PubQueue-->>PubThread: PublishMessage (or nullptr)
        deactivate PubQueue
        
        alt No Message Available
            PubThread->>PubThread: Adaptive Sleep (CPU Mode)
        else Message Available
            PubThread->>ZMQ_Pub: Send Topic Frame (exec.report/exec.fill)
            activate ZMQ_Pub
            ZMQ_Pub-->>PubThread: Frame Sent
            PubThread->>ZMQ_Pub: Send Payload Frame (JSON)
            ZMQ_Pub->>Subscriber: Multicast to Subscribers
            activate Subscriber
            Subscriber->>Subscriber: Process Report/Fill
            deactivate Subscriber
            ZMQ_Pub-->>PubThread: Frame Sent
            deactivate ZMQ_Pub
            
            PubThread->>Pool: Return PublishMessage to Pool
            activate Pool
            Pool-->>PubThread: Returned
            deactivate Pool
            
            PubThread->>PubThread: Increment Published Stats
        end
        deactivate PubThread
    end

    Note over Client,Subscriber: === END-TO-END LATENCY: ~10-70 μs ===
```

---

## Component Interaction Summary

### 1. **Order Ingestion Path** (Synchronous)
```
Client → ZMQ PULL → RecvThread → Parser → Pool → Processor → Dispatcher
```
- **Latency**: ~1-3 μs (parse + validate)
- **Characteristics**: Zero-copy, lock-free, single-threaded

### 2. **Venue Routing & Adapter** (Synchronous)
```
Processor → Router → Adapter → Connector → RestAPI → Exchange
```
- **Latency**: ~5-50 μs (mostly network + REST API)
- **Characteristics**: Abstraction layers, error propagation

### 3. **Order Tracking** (State Management)
```
Connector → ClientOrderTracker → InFlightOrder
```
- **Latency**: ~0.1-0.5 μs (in-memory state update)
- **Characteristics**: Thread-safe, state machine, lifecycle management

### 4. **Publishing Path** (Asynchronous)
```
Processor → Serializer → PubQueue → PubThread → ZMQ PUB → Subscribers
```
- **Latency**: ~1-5 μs (serialize + queue + publish)
- **Characteristics**: Lock-free SPSC queue, separate RT thread

### 5. **User Stream Updates** (Asynchronous)
```
WSStream → Connector → Adapter → Processor → Serializer → PubQueue
```
- **Latency**: ~2-10 μs (callback + normalize + serialize)
- **Characteristics**: Event-driven, lazy rehydration, idempotent

### 6. **Fill Events** (Asynchronous)
```
WSStream → Connector → Tracker → Adapter → Processor → Serializer → PubQueue
```
- **Latency**: ~2-8 μs (parse + tag + serialize)
- **Characteristics**: Execution tracking, fee accounting

---

## Critical Interaction Points

### 🔥 Hot Path (Order Placement)
1. **ZMQ Recv** → Non-blocking, EAGAIN handling
2. **JSON Parse** → Zero-copy string_view, RapidJSON SAX
3. **Action Dispatch** → FNV-1a compile-time hash, branch-free
4. **Adapter Call** → Virtual function (predicted by CPU)
5. **REST API** → Boost.Beast connection pooling, keep-alive
6. **Serialize** → RapidJSON StringBuffer, pre-sized
7. **Queue Push** → Lock-free CAS, single producer
8. **ZMQ Pub** → Zero-copy frames, multicast

### 🔄 Async Path (Updates & Fills)
1. **WebSocket Event** → Boost.Beast async read
2. **Connector Parse** → Exchange-specific JSON parsing
3. **Tracker Update** → Mutex-protected state transition
4. **Callback Invoke** → Direct function call
5. **Lazy Rehydration** → REST query for unknown orders
6. **Normalize** → Status/reason mapping via lookup tables
7. **Publish** → Same SPSC queue path as orders

### 🧵 Threading Model
- **Core 2 (RT)**: Order Receiver + Processing (synchronous hot path)
- **Core 3 (RT)**: Publisher (async queue consumer)
- **Connector Threads**: WebSocket I/O + User Stream processing
- **Stats Thread**: Background monitoring (no RT priority)

---

## Interaction Patterns

### Pattern 1: Request-Response (Synchronous)
```
Order Placement, Cancellation, Modification
Processor → Adapter → Connector → REST → Exchange → Response
```
- Blocking wait for REST response
- Timeout handling
- Immediate acceptance/rejection report

### Pattern 2: Fire-and-Forget + Async Callback (Hybrid)
```
Order Placement → Acceptance Report (sync)
WebSocket Update → Execution Report (async)
```
- Initial synchronous confirmation
- Subsequent async state updates
- Multiple callbacks per order lifecycle

### Pattern 3: Publish-Subscribe (Async)
```
Processor → PubQueue → PubThread → ZMQ PUB → Multiple Subscribers
```
- Decoupled producer/consumer
- Lock-free queue
- Multicast distribution

### Pattern 4: Lazy Loading (On-Demand)
```
Unknown Order Update → query_order → REST → Rehydrate → Process
```
- Fault-tolerant
- Handles external/manual trades
- Transparent to upstream clients

---

## Data Flow Characteristics

| Flow Stage | Concurrency | Memory | Latency | Error Handling |
|------------|-------------|---------|---------|----------------|
| **Recv** | Single-threaded | Stack | <1 μs | EAGAIN retry |
| **Parse** | Lock-free | Pool | 1-3 μs | Rejection report |
| **Process** | Single-threaded | Stack + Cache | 0.5-2 μs | Validation + reject |
| **Adapter** | Virtual call | Heap (connector) | 5-50 μs | Exception + error code |
| **REST** | Async I/O | Buffer pool | 1-20 ms | Timeout + retry |
| **Callback** | Thread-safe | Mutex-protected | 2-10 μs | Error callback |
| **Serialize** | Lock-free | String buffer | 1-2 μs | Buffer overflow check |
| **Publish** | SPSC queue | Lock-free | 1-5 μs | Queue full drop |

---

## Performance Bottlenecks

### Identified Bottlenecks
1. **REST API Call** (1-20 ms): Network latency to exchange
2. **Pool Exhaustion** (rare): Backpressure when pool full
3. **Queue Full** (rare): Publisher thread can't keep up
4. **Mutex Contention** (low): ClientOrderTracker update lock

### Mitigation Strategies
1. **Connection Pooling**: Reuse HTTPS connections
2. **Pre-warming**: Allocate pools at startup
3. **Adaptive Sleep**: CPU mode-aware publisher thread
4. **Lock-Free Queues**: Avoid mutex on hot path
5. **TSC Timestamps**: Avoid syscalls for timing

---

## Failure Modes & Recovery

### Mode 1: Pool Exhaustion
**Symptom**: Cannot allocate Order/Report/Fill
**Recovery**: Log error, drop message, increment stats
**Impact**: Lost order (client retry required)

### Mode 2: Publish Queue Full
**Symptom**: Cannot push to PubQueue
**Recovery**: Log warning, drop message, increment stats
**Impact**: Lost report/fill (client misses update)

### Mode 3: Exchange API Error
**Symptom**: REST call returns error
**Recovery**: Send rejection report with reason code
**Impact**: Order not placed (graceful failure)

### Mode 4: Unknown Order Update
**Symptom**: Update for order not in pending_orders_
**Recovery**: Lazy rehydration via query_order
**Impact**: Additional REST call (~5-20 ms delay)

### Mode 5: WebSocket Disconnect
**Symptom**: User stream connection lost
**Recovery**: Connector auto-reconnect + full rehydration
**Impact**: Missed updates (replay on reconnect)

---

## End-to-End Latency Budget

```
Total: 10-70 μs (excluding network to exchange)

Breakdown:
├─ ZMQ Recv:           0.5-1 μs
├─ JSON Parse:         1-3 μs
├─ Validation:         0.5-1 μs
├─ Action Dispatch:    0.1-0.3 μs
├─ Adapter Call:       0.5-1 μs
├─ REST API:           5-50 μs  ← BOTTLENECK
├─ Serialize Report:   1-2 μs
├─ Queue Push:         0.1-0.5 μs
├─ ZMQ Publish:        1-5 μs
└─ Total:              ~10-70 μs

Plus:
├─ Network to Exchange:  1-20 ms  ← NETWORK BOTTLENECK
├─ Exchange Processing:  100 μs - 10 ms
└─ Network from Exchange: 1-20 ms
```

---

## Monitoring & Observability

### Stats Collected (Atomic Counters)
- `orders_received_count`: Total orders received
- `orders_accepted_count`: Successfully placed
- `orders_rejected_count`: Validation/exchange errors
- `reports_published_count`: Execution reports sent
- `fills_published_count`: Fill events sent
- `pool_exhausted_count`: Pool allocation failures
- `queue_full_count`: Publish queue overflows
- `min_latency_ns`, `max_latency_ns`, `total_latency_ns`: Latency tracking

### Logged Every 10s
- Orders/sec rate
- Average/min/max latency (μs)
- Pool utilization %
- Queue depth

### Integration Points
- **spdlog**: Structured logging
- **Future**: Prometheus metrics export
- **Future**: OpenTelemetry tracing
