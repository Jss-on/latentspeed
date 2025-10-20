# Phase 5: Event-Driven Order Lifecycle

**Duration**: Week 4  
**Priority**: 🟡 Medium  
**Dependencies**: All previous phases

## Objectives

1. Implement complete order placement flow (Hummingbot pattern)
2. Event emission on state changes
3. WebSocket user stream integration
4. Error handling and retry logic

---

## Complete Order Placement Flow (Hummingbot Pattern)

### Sequence Diagram

```
Strategy/User
    │
    ├─► buy(params) ──────────────────────────► Connector
    │                                               │
    │   ◄─── client_order_id (immediate) ─────────┤
    │                                               │
    │                                               ├─► generate_client_order_id()
    │                                               │
    │                                               ├─► create InFlightOrder
    │                                               │
    │                                               ├─► start_tracking_order()  [BEFORE API!]
    │                                               │
    │                                               ├─► schedule async:
    │                                               │   _place_order_and_process_update()
    │                                               │        │
    │                                               │        ├─► validate params
    │                                               │        │
    │                                               │        ├─► _place_order()
    │                                               │        │        │
    │                                               │        │        ├─► sign request
    │                                               │        │        │
    │                                               │        │        ├─► POST to exchange
    │                                               │        │        │
    │                                               │        │        └─► parse response
    │                                               │        │
    │                                               │        ├─► process_order_update()
    │   ◄─── OrderCreatedEvent ──────────────────────────────┤
    │                                               │        │
    │                                               │        └─► emit event
    │                                               │
    │   [WebSocket user stream running in parallel]
    │                                               │
    │   ◄─── OrderFilledEvent ────────────────────────────── WS: fill message
    │                                               │
    │   ◄─── OrderCompletedEvent ─────────────────────────── fully filled
```

---

## 1. Core Implementation: buy() / sell()

**In connector implementation**:

```cpp
std::string HyperliquidPerpetualConnector::buy(const OrderParams& params) {
    // 1. Generate client order ID (unique, includes prefix + counter)
    std::string client_order_id = generate_client_order_id();
    
    // 2. Validate params
    if (!validate_order_params(params)) {
        emit_order_failure_event(client_order_id, "Invalid order parameters");
        return client_order_id;
    }
    
    // 3. Apply trading rules (quantization)
    double quantized_price = quantize_order_price(params.trading_pair, params.price);
    double quantized_amount = quantize_order_amount(params.trading_pair, params.amount);
    
    // 4. Create InFlightOrder
    InFlightOrder order{
        .client_order_id = client_order_id,
        .trading_pair = params.trading_pair,
        .order_type = params.order_type,
        .trade_type = TradeType::BUY,
        .position_action = params.position_action,
        .price = quantized_price,
        .amount = quantized_amount,
        .creation_timestamp = current_timestamp_ns()
    };
    
    // Add exchange-specific fields
    if (params.extra_params.count("cloid")) {
        order.cloid = params.extra_params.at("cloid");
    } else {
        order.cloid = client_order_id;  // Use client_order_id as cloid
    }
    
    // 5. START TRACKING BEFORE API CALL (Hummingbot critical pattern!)
    order_tracker_.start_tracking(std::move(order));
    
    // 6. Schedule async submission
    async_executor_->submit([this, client_order_id]() -> Task<void> {
        try {
            co_await _place_order_and_process_update(client_order_id);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to place order {}: {}", client_order_id, e.what());
            // Error already handled in _place_order_and_process_update
        }
    });
    
    // 7. Return immediately (NON-BLOCKING!)
    return client_order_id;
}
```

---

## 2. Internal: _place_order_and_process_update()

```cpp
Task<void> HyperliquidPerpetualConnector::_place_order_and_process_update(
    const std::string& client_order_id
) {
    auto order_opt = order_tracker_.get_order(client_order_id);
    if (!order_opt.has_value()) {
        LOG_ERROR("Order {} not found in tracker", client_order_id);
        co_return;
    }
    
    InFlightOrder order = *order_opt;
    
    try {
        // Update state to PENDING_SUBMIT
        OrderUpdate pending_update{
            .client_order_id = client_order_id,
            .new_state = OrderState::PENDING_SUBMIT,
            .update_timestamp = current_timestamp_ns()
        };
        order_tracker_.process_order_update(pending_update);
        
        // Call exchange API
        auto [exchange_order_id, timestamp] = co_await _place_order(
            order.client_order_id,
            order.trading_pair,
            order.amount,
            order.trade_type,
            order.order_type,
            order.price,
            order.position_action
        );
        
        // Process success
        OrderUpdate success_update{
            .client_order_id = client_order_id,
            .exchange_order_id = exchange_order_id,
            .trading_pair = order.trading_pair,
            .new_state = OrderState::OPEN,
            .update_timestamp = timestamp
        };
        order_tracker_.process_order_update(success_update);
        
        // Emit event
        emit_order_created_event(client_order_id, exchange_order_id);
        
        LOG_INFO("Order {} created successfully with exchange ID {}", 
                 client_order_id, exchange_order_id);
        
    } catch (const std::exception& e) {
        // Process failure
        OrderUpdate failure_update{
            .client_order_id = client_order_id,
            .new_state = OrderState::FAILED,
            .update_timestamp = current_timestamp_ns(),
            .reason = e.what()
        };
        order_tracker_.process_order_update(failure_update);
        
        // Emit event
        emit_order_failure_event(client_order_id, e.what());
        
        LOG_ERROR("Order {} failed: {}", client_order_id, e.what());
    }
}
```

---

## 3. Exchange API Call: _place_order()

### Hyperliquid Implementation

```cpp
Task<std::pair<std::string, uint64_t>> HyperliquidPerpetualConnector::_place_order(
    const std::string& order_id,
    const std::string& trading_pair,
    double amount,
    TradeType trade_type,
    OrderType order_type,
    double price,
    PositionAction position_action
) {
    // 1. Get asset index
    std::string symbol = co_await exchange_symbol_associated_to_pair(trading_pair);
    std::string coin = symbol.substr(0, symbol.find('-'));
    
    if (coin_to_asset_.find(coin) == coin_to_asset_.end()) {
        throw std::runtime_error("Unknown asset: " + coin);
    }
    int asset_index = coin_to_asset_[coin];
    
    // 2. Map order type to Hyperliquid format
    nlohmann::json param_order_type = {{"limit", {{"tif", "Gtc"}}}};
    if (order_type == OrderType::LIMIT_MAKER) {
        param_order_type = {{"limit", {{"tif", "Alo"}}}};  // Post-only
    } else if (order_type == OrderType::MARKET) {
        param_order_type = {{"limit", {{"tif", "Ioc"}}}};  // Immediate or cancel
    }
    
    // 3. Build request
    nlohmann::json api_params = {
        {"type", "order"},
        {"grouping", "na"},
        {"orders", {
            {"asset", asset_index},
            {"isBuy", trade_type == TradeType::BUY},
            {"limitPx", price},           // Float (converted to string in web_utils)
            {"sz", amount},               // Float (converted to string in web_utils)
            {"reduceOnly", position_action == PositionAction::CLOSE},
            {"orderType", param_order_type},
            {"cloid", order_id}
        }}
    };
    
    // 4. Sign and send
    auto order_result = co_await api_post(
        CREATE_ORDER_URL,
        api_params,
        true  // is_auth_required
    );
    
    // 5. Parse response
    if (order_result["status"] == "err") {
        throw std::runtime_error(order_result["response"].dump());
    }
    
    const auto& status = order_result["response"]["data"]["statuses"][0];
    if (status.contains("error")) {
        throw std::runtime_error(status["error"].get<std::string>());
    }
    
    // Extract exchange order ID
    std::string exchange_order_id;
    if (status.contains("resting")) {
        exchange_order_id = std::to_string(status["resting"]["oid"].get<int64_t>());
    } else if (status.contains("filled")) {
        exchange_order_id = std::to_string(status["filled"]["oid"].get<int64_t>());
    } else {
        throw std::runtime_error("Unexpected order status");
    }
    
    co_return std::make_pair(exchange_order_id, current_timestamp_ns());
}
```

### dYdX v4 Implementation

```cpp
Task<std::pair<std::string, uint64_t>> DydxV4PerpetualConnector::_place_order(
    const std::string& order_id,
    const std::string& trading_pair,
    double amount,
    TradeType trade_type,
    OrderType order_type,
    double price,
    PositionAction position_action
) {
    // Get market metadata
    const auto& market_info = margin_fractions_[trading_pair];
    
    // Convert to integer client ID
    int client_id = std::stoi(order_id);  // Assumes numeric order IDs for dYdX
    
    // Determine type
    std::string type = order_type.is_limit_type() ? "LIMIT" : "MARKET";
    
    // Adjust price for market orders
    if (type == "MARKET") {
        if (trade_type == TradeType::BUY) {
            price = get_price_for_volume(trading_pair, true, amount).result_price * 1.5;
        } else {
            price = get_price_for_volume(trading_pair, false, amount).result_price * 0.75;
        }
        price = quantize_order_price(trading_pair, price);
    }
    
    // Call dYdX client with retry logic
    std::string side = (trade_type == TradeType::BUY) ? "BUY" : "SELL";
    bool post_only = (order_type == OrderType::LIMIT_MAKER);
    bool reduce_only = (position_action == PositionAction::CLOSE);
    
    auto result = co_await tx_client_->place_order(
        trading_pair,
        type,
        side,
        price,
        amount,
        client_id,
        post_only,
        reduce_only
    );
    
    // Check for errors
    if (result["raw_log"] != "[]" || result["txhash"].empty()) {
        throw std::runtime_error("Transaction failed: " + result["raw_log"].dump());
    }
    
    // dYdX doesn't return exchange order ID immediately
    // It will come via user stream
    co_return std::make_pair("", current_timestamp_ns());
}
```

---

## 4. WebSocket User Stream Integration

```cpp
Task<void> HyperliquidPerpetualConnector::_user_stream_event_listener() {
    while (is_connected()) {
        try {
            auto event = co_await user_stream_tracker_->get_next_message();
            
            std::string channel = event["channel"];
            
            if (channel == "userOrders") {
                for (const auto& order_msg : event["data"]) {
                    process_order_message_from_ws(order_msg);
                }
            } else if (channel == "userEvents") {
                if (event["data"].contains("fills")) {
                    for (const auto& fill_msg : event["data"]["fills"]) {
                        co_await process_trade_message_from_ws(fill_msg);
                    }
                }
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error in user stream: {}", e.what());
            co_await sleep_for(std::chrono::seconds(1));
        }
    }
}

void HyperliquidPerpetualConnector::process_order_message_from_ws(
    const nlohmann::json& order_msg
) {
    std::string exchange_order_id = std::to_string(order_msg["oid"].get<int64_t>());
    std::string status = order_msg["status"];  // "open", "filled", "cancelled"
    
    // Find order by exchange ID
    auto order_opt = order_tracker_.get_order_by_exchange_id(exchange_order_id);
    if (!order_opt.has_value()) {
        LOG_WARN("Received update for unknown exchange order ID: {}", exchange_order_id);
        return;
    }
    
    const auto& order = *order_opt;
    
    // Map status to OrderState
    OrderState new_state = OrderState::OPEN;
    if (status == "filled") {
        new_state = OrderState::FILLED;
    } else if (status == "cancelled" || status == "rejected") {
        new_state = OrderState::CANCELLED;
    }
    
    // Process update
    OrderUpdate update{
        .client_order_id = order.client_order_id,
        .exchange_order_id = exchange_order_id,
        .trading_pair = order.trading_pair,
        .new_state = new_state,
        .update_timestamp = current_timestamp_ns()
    };
    
    order_tracker_.process_order_update(update);
}

Task<void> HyperliquidPerpetualConnector::process_trade_message_from_ws(
    const nlohmann::json& fill_msg
) {
    std::string exchange_order_id = std::to_string(fill_msg["oid"].get<int64_t>());
    
    auto order_opt = order_tracker_.get_order_by_exchange_id(exchange_order_id);
    if (!order_opt.has_value()) {
        return;
    }
    
    const auto& order = *order_opt;
    
    // Build TradeUpdate
    TradeUpdate trade{
        .trade_id = std::to_string(fill_msg["tid"].get<int64_t>()),
        .client_order_id = order.client_order_id,
        .exchange_order_id = exchange_order_id,
        .trading_pair = order.trading_pair,
        .fill_price = fill_msg["px"].get<double>(),
        .fill_base_amount = fill_msg["sz"].get<double>(),
        .fill_quote_amount = fill_msg["px"].get<double>() * fill_msg["sz"].get<double>(),
        .fee_currency = order.trading_pair.substr(order.trading_pair.find('-') + 1),
        .fee_amount = fill_msg["fee"].get<double>(),
        .liquidity = (fill_msg["dir"].get<std::string>().find("Open") != std::string::npos) 
            ? "maker" : "taker",
        .fill_timestamp = fill_msg["time"].get<uint64_t>() * 1000000  // Convert ms to ns
    };
    
    order_tracker_.process_trade_update(trade);
    
    co_return;
}
```

---

## 5. Event Emission

```cpp
void HyperliquidPerpetualConnector::emit_order_created_event(
    const std::string& client_order_id,
    const std::string& exchange_order_id
) {
    if (order_event_listener_) {
        order_event_listener_->on_order_created(client_order_id, exchange_order_id);
    }
    
    // Also publish to ZMQ if configured
    if (zmq_publisher_) {
        nlohmann::json event = {
            {"type", "ORDER_CREATED"},
            {"client_order_id", client_order_id},
            {"exchange_order_id", exchange_order_id},
            {"timestamp", current_timestamp_ns()}
        };
        zmq_publisher_->publish("order_events", event.dump());
    }
}

void HyperliquidPerpetualConnector::emit_order_failure_event(
    const std::string& client_order_id,
    const std::string& reason
) {
    if (order_event_listener_) {
        order_event_listener_->on_order_failed(client_order_id, reason);
    }
    
    if (zmq_publisher_) {
        nlohmann::json event = {
            {"type", "ORDER_FAILED"},
            {"client_order_id", client_order_id},
            {"reason", reason},
            {"timestamp", current_timestamp_ns()}
        };
        zmq_publisher_->publish("order_events", event.dump());
    }
}
```

---

## 6. Error Handling Strategies

### Retry Logic (dYdX v4)

```cpp
// Already implemented in dYdX client's place_order():
for (int i = 0; i < 3; ++i) {
    auto result = co_await tx_client_->place_order(...);
    
    if (result["raw_log"].get<std::string>().find("sequence mismatch") != std::string::npos) {
        LOG_WARN("Sequence mismatch on attempt {}/3", i + 1);
        co_await tx_client_->initialize_trading_account();
        co_await sleep_for(std::chrono::seconds(1));
        continue;
    }
    
    co_return result;
}
```

### Timeout Handling

```cpp
Task<std::pair<std::string, uint64_t>> _place_order_with_timeout(
    const std::string& order_id,
    ...
) {
    auto future = _place_order(order_id, ...);
    
    auto timeout = std::chrono::seconds(5);
    auto status = co_await future.wait_for(timeout);
    
    if (status == std::future_status::timeout) {
        throw std::runtime_error("Order placement timeout");
    }
    
    co_return future.get();
}
```

### Network Error Recovery

```cpp
Task<void> handle_network_error(const std::exception& e) {
    LOG_ERROR("Network error: {}", e.what());
    
    // Reconnect WebSocket
    if (!user_stream_tracker_->is_connected()) {
        co_await user_stream_tracker_->reconnect();
    }
    
    // Resubscribe to channels
    co_await user_stream_tracker_->resubscribe();
    
    // Sync order states via REST
    co_await sync_open_orders_from_rest();
}
```

---

## Summary

This phase completes the event-driven order lifecycle by:

1. **Non-blocking order placement**: Returns client_order_id immediately
2. **Pre-tracking**: Starts tracking BEFORE API call
3. **Async execution**: Order submission happens in background
4. **WebSocket updates**: Real-time state changes via user stream
5. **Event emission**: Listeners notified on every state change
6. **Error handling**: Retry logic, timeouts, network recovery

---

## Next: Migration Strategy

See [07_MIGRATION_STRATEGY.md](07_MIGRATION_STRATEGY.md) for:
- Week-by-week implementation plan
- Backward compatibility approach
- Testing strategy
- Rollout plan
