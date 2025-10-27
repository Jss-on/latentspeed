/**
 * @file hyperliquid_integrated_connector.cpp
 * @brief Implementation of Hyperliquid Integrated Connector
 */

#include "connector/exchange/hyperliquid/hyperliquid_integrated_connector.h"
#include <spdlog/spdlog.h>

namespace latentspeed {

// Constructor
HyperliquidIntegratedConnector::HyperliquidIntegratedConnector(
    std::shared_ptr<HyperliquidAuth> auth,
    std::shared_ptr<HyperliquidExchange> existing_exchange,
    std::shared_ptr<zmq::context_t> zmq_context,
    const std::string& zmq_endpoint,
    bool testnet
)   : auth_(auth),
      existing_exchange_(existing_exchange),
      testnet_(testnet),
      work_guard_(net::make_work_guard(io_context_)),
      order_tracker_(std::make_shared<ClientOrderTracker>()) {
    
    if (!auth_) {
        throw std::invalid_argument("HyperliquidAuth cannot be null");
    }
    if (!existing_exchange_) {
        throw std::invalid_argument("HyperliquidExchange cannot be null");
    }
    if (!zmq_context) {
        throw std::invalid_argument("ZMQ context cannot be null");
    }
    
    // Create marketstream adapter (wraps your existing exchange)
    marketstream_adapter_ = std::make_shared<HyperliquidMarketstreamAdapter>(existing_exchange_);
    
    // Create user stream (Phase 5 - for authenticated data)
    user_stream_ = std::make_shared<HyperliquidUserStreamDataSource>(
        auth_->get_address(),
        testnet_
    );
    
    // Create ZMQ publisher
    zmq_publisher_ = std::make_shared<ZMQOrderEventPublisher>(
        zmq_context,
        zmq_endpoint,
        "orders.hyperliquid"
    );
    
    // Set up user stream callbacks
    setup_user_stream_callbacks();
    
    // Set up order tracker event callbacks
    setup_order_tracker_callbacks();
}

// Destructor
HyperliquidIntegratedConnector::~HyperliquidIntegratedConnector() {
    stop();
}

// Lifecycle Methods

bool HyperliquidIntegratedConnector::initialize() {
    spdlog::info("HyperliquidIntegratedConnector: Initializing...");
    
    // Initialize marketstream adapter
    if (!marketstream_adapter_->initialize()) {
        spdlog::error("Failed to initialize marketstream adapter");
        return false;
    }
    
    // Initialize user stream
    if (!user_stream_->initialize()) {
        spdlog::error("Failed to initialize user stream");
        return false;
    }
    
    // Fetch trading rules
    if (!fetch_trading_rules()) {
        spdlog::error("Failed to fetch trading rules");
        return false;
    }
    
    spdlog::info("HyperliquidIntegratedConnector: Initialized successfully");
    return true;
}

void HyperliquidIntegratedConnector::start() {
    if (running_) {
        spdlog::warn("Connector already running");
        return;
    }
    
    spdlog::info("HyperliquidIntegratedConnector: Starting...");
    
    running_ = true;
    
    // Start marketstream adapter (your existing market data)
    marketstream_adapter_->start();
    
    // Start user stream (Phase 5 authenticated stream)
    user_stream_->start();
    
    // Start async worker thread
    async_thread_ = std::thread([this]() {
        spdlog::info("Async worker thread started");
        io_context_.run();
        spdlog::info("Async worker thread stopped");
    });
    
    spdlog::info("HyperliquidIntegratedConnector: Started successfully");
}

void HyperliquidIntegratedConnector::stop() {
    if (!running_) {
        return;
    }
    
    spdlog::info("HyperliquidIntegratedConnector: Stopping...");
    
    running_ = false;
    
    // Stop user stream
    if (user_stream_) {
        user_stream_->stop();
    }
    
    // Stop marketstream adapter
    if (marketstream_adapter_) {
        marketstream_adapter_->stop();
    }
    
    // Stop async worker
    work_guard_.reset();
    if (async_thread_.joinable()) {
        async_thread_.join();
    }
    
    spdlog::info("HyperliquidIntegratedConnector: Stopped");
}

// Order Operations

std::string HyperliquidIntegratedConnector::buy(const OrderParams& params) {
    return place_order(params, TradeType::BUY);
}

std::string HyperliquidIntegratedConnector::sell(const OrderParams& params) {
    return place_order(params, TradeType::SELL);
}

std::future<bool> HyperliquidIntegratedConnector::cancel(
    const std::string& trading_pair,
    const std::string& client_order_id) {
    
    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();
    
    net::post(io_context_, [this, trading_pair, client_order_id, promise]() {
        try {
            bool success = execute_cancel_order(trading_pair, client_order_id);
            promise->set_value(success);
        } catch (const std::exception& e) {
            spdlog::error("Exception canceling order {}: {}", client_order_id, e.what());
            promise->set_exception(std::current_exception());
        }
    });
    
    return future;
}

// Query Methods

std::vector<InFlightOrder> HyperliquidIntegratedConnector::get_open_orders(
    const std::string& trading_pair) const {
    return order_tracker_->all_fillable_orders(trading_pair);
}

std::optional<InFlightOrder> HyperliquidIntegratedConnector::get_order(
    const std::string& client_order_id) const {
    return order_tracker_->get_order(client_order_id);
}

// Connector Info

std::string HyperliquidIntegratedConnector::connector_name() const {
    return testnet_ ? "hyperliquid_testnet_integrated" : "hyperliquid_integrated";
}

bool HyperliquidIntegratedConnector::is_connected() const {
    return marketstream_adapter_->is_connected() && user_stream_->is_connected();
}

// Component Access

std::shared_ptr<HyperliquidExchange> HyperliquidIntegratedConnector::get_marketstream_exchange() const {
    return existing_exchange_;
}

std::shared_ptr<ZMQOrderEventPublisher> HyperliquidIntegratedConnector::get_zmq_publisher() const {
    return zmq_publisher_;
}

// Private Methods

std::string HyperliquidIntegratedConnector::place_order(
    const OrderParams& params,
    TradeType trade_type) {
    
    // Generate client order ID
    std::string client_order_id = params.client_order_id.empty() 
        ? generate_client_order_id()
        : params.client_order_id;
    
    // Validate params
    if (!validate_order_params(params)) {
        spdlog::error("Invalid order parameters for {}", client_order_id);
        emit_order_failure_event(client_order_id, "Invalid parameters");
        return client_order_id;
    }
    
    // Create InFlightOrder
    InFlightOrder order;
    order.client_order_id = client_order_id;
    order.trading_pair = params.trading_pair;
    order.order_type = params.order_type;
    order.trade_type = trade_type;
    order.amount = quantize_order_amount(params.trading_pair, params.amount);
    order.price = (params.order_type == OrderType::MARKET) 
        ? 0.0 
        : quantize_order_price(params.trading_pair, params.price);
    order.position_action = params.position_action;
    order.creation_timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    order.current_state = OrderState::PENDING_CREATE;
    
    // ⭐ CRITICAL: Track BEFORE submitting to exchange
    order_tracker_->start_tracking(std::move(order));
    
    // Schedule async submission
    net::post(io_context_, [this, client_order_id]() {
        place_order_and_process_update(client_order_id);
    });
    
    // Return immediately (non-blocking!)
    return client_order_id;
}

void HyperliquidIntegratedConnector::place_order_and_process_update(
    const std::string& client_order_id) {
    
    auto order_opt = order_tracker_->get_order(client_order_id);
    if (!order_opt) {
        spdlog::error("Order {} not found in tracker", client_order_id);
        return;
    }
    
    InFlightOrder order = *order_opt;
    
    // Update state to PENDING_SUBMIT
    OrderUpdate update;
    update.client_order_id = client_order_id;
    update.new_state = OrderState::PENDING_SUBMIT;
    order_tracker_->process_order_update(update);
    
    // Execute REST API call
    try {
        std::string exchange_order_id = execute_place_order(order);
        
        if (!exchange_order_id.empty()) {
            // Success - update with exchange order ID
            update.exchange_order_id = exchange_order_id;
            update.new_state = OrderState::OPEN;
            order_tracker_->process_order_update(update);
            
            spdlog::info("Order {} placed successfully, exchange_order_id: {}", 
                       client_order_id, exchange_order_id);
        } else {
            // Failed
            update.new_state = OrderState::FAILED;
            order_tracker_->process_order_update(update);
            
            spdlog::error("Order {} placement failed", client_order_id);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Exception placing order {}: {}", client_order_id, e.what());
        
        update.new_state = OrderState::FAILED;
        order_tracker_->process_order_update(update);
    }
}

std::string HyperliquidIntegratedConnector::execute_place_order(const InFlightOrder& order) {
    // Implementation similar to Phase 5 HyperliquidPerpetualConnector
    // Uses HyperliquidAuth to sign and make REST call
    // Returns exchange_order_id on success, empty string on failure
    
    // TODO: Implement actual REST API call
    spdlog::warn("execute_place_order not yet fully implemented");
    return "";  // Placeholder
}

bool HyperliquidIntegratedConnector::execute_cancel_order(
    const std::string& trading_pair,
    const std::string& client_order_id) {
    
    // Implementation for cancellation
    // TODO: Implement actual REST API call
    spdlog::warn("execute_cancel_order not yet fully implemented");
    return false;  // Placeholder
}

void HyperliquidIntegratedConnector::setup_user_stream_callbacks() {
    user_stream_->set_message_callback([this](const UserStreamMessage& msg) {
        handle_user_stream_message(msg);
    });
}

void HyperliquidIntegratedConnector::handle_user_stream_message(const UserStreamMessage& msg) {
    if (msg.type == UserStreamMessageType::ORDER_UPDATE) {
        process_order_update(msg);
    } else if (msg.type == UserStreamMessageType::TRADE_UPDATE) {
        process_trade_update(msg);
    }
}

void HyperliquidIntegratedConnector::process_order_update(const UserStreamMessage& msg) {
    // Convert msg to OrderUpdate and process
    OrderUpdate update;
    // TODO: Parse message properly
    
    order_tracker_->process_order_update(update);
}

void HyperliquidIntegratedConnector::process_trade_update(const UserStreamMessage& msg) {
    // Convert msg to TradeUpdate and process
    TradeUpdate trade;
    // TODO: Parse message properly
    
    order_tracker_->process_trade_update(trade);
}

void HyperliquidIntegratedConnector::setup_order_tracker_callbacks() {
    order_tracker_->set_event_callback([this](OrderEventType event_type, const InFlightOrder& order) {
        // Publish to ZMQ based on event type
        switch (event_type) {
            case OrderEventType::CREATED:
                zmq_publisher_->publish_order_created(order);
                spdlog::info("Published order_created: {}", order.client_order_id);
                break;
                
            case OrderEventType::FILLED:
                zmq_publisher_->publish_order_filled(order);
                spdlog::info("Published order_filled: {}", order.client_order_id);
                break;
                
            case OrderEventType::PARTIALLY_FILLED:
                zmq_publisher_->publish_order_update(order);
                spdlog::info("Published order_partially_filled: {}", order.client_order_id);
                break;
                
            case OrderEventType::CANCELLED:
                zmq_publisher_->publish_order_cancelled(order);
                spdlog::info("Published order_cancelled: {}", order.client_order_id);
                break;
                
            case OrderEventType::FAILED:
                zmq_publisher_->publish_order_failed(order, "Order failed");
                spdlog::info("Published order_failed: {}", order.client_order_id);
                break;
                
            default:
                zmq_publisher_->publish_order_update(order);
                break;
        }
    });
}

bool HyperliquidIntegratedConnector::fetch_trading_rules() {
    // Fetch from your existing exchange or REST API
    // TODO: Implement actual fetch
    return true;  // Placeholder
}

bool HyperliquidIntegratedConnector::validate_order_params(const OrderParams& params) const {
    if (params.trading_pair.empty()) return false;
    if (params.amount <= 0.0) return false;
    if (params.order_type != OrderType::MARKET && params.price <= 0.0) return false;
    return true;
}

void HyperliquidIntegratedConnector::emit_order_failure_event(
    const std::string& client_order_id,
    const std::string& reason) {
    
    InFlightOrder failed_order;
    failed_order.client_order_id = client_order_id;
    failed_order.current_state = OrderState::FAILED;
    zmq_publisher_->publish_order_failed(failed_order, reason);
}

}  // namespace latentspeed
