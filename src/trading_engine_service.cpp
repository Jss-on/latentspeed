/**
 * @file trading_engine_service.cpp
 * @brief Live trading engine service with CCAPI integration
 * @author jessiondiwangan@gmail.com
 * @date 2025
 * 
 * LIVE TRADING VERSION - Includes functionality for:
 * - Real exchange connectivity via CCAPI
 * - Live order placement and management
 * - Real-time execution reports and fills
 * - Integration with Python strategies
 */

#include "trading_engine_service.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <random>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <unordered_set>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>

namespace latentspeed {

/**
 * @brief Live trading constructor - sets up exchange connectivity
 */
TradingEngineService::TradingEngineService()
    : running_(false)
    , order_endpoint_("tcp://127.0.0.1:5601")        // Contract-specified endpoint
    , report_endpoint_("tcp://127.0.0.1:5602")       // Contract-specified endpoint
{
    spdlog::info("[TradingEngine] Live trading engine initialized");
    spdlog::info("[TradingEngine] Mode: Live exchange connectivity");
}

TradingEngineService::~TradingEngineService() {
    stop();
}

/**
 * @brief Initialize live trading engine with direct exchange clients
 */
bool TradingEngineService::initialize() {
    try {
        spdlog::info("[TradingEngine] Starting initialization...");
        
        // Initialize ZeroMQ context
        spdlog::info("[TradingEngine] Creating ZeroMQ context...");
        zmq_context_ = std::make_unique<zmq::context_t>(1);
        spdlog::info("[TradingEngine] ZeroMQ context created successfully");
        
        // Order receiver socket (PULL - receives ExecutionOrders from Python)
        spdlog::info("[TradingEngine] Creating order receiver socket...");
        order_receiver_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PULL);
        order_receiver_socket_->bind(order_endpoint_);
        spdlog::info("[TradingEngine] Order receiver socket bound to {}", order_endpoint_);
        
        // Report publisher socket (PUB - sends ExecutionReports and Fills to Python)
        spdlog::info("[TradingEngine] Creating report publisher socket...");
        report_publisher_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PUB);
        report_publisher_socket_->bind(report_endpoint_);
        spdlog::info("[TradingEngine] Report publisher socket bound to {}", report_endpoint_);
        
        // Initialize Bybit client with demo credentials
        spdlog::info("[TradingEngine] Initializing Bybit client...");
        bybit_client_ = std::make_unique<BybitClient>();
        
        // Configure Bybit demo credentials (arkpad-test account)  
        std::string api_key = "xgI7ixEhDcOZ5Sr0Sb";
        std::string api_secret = "JHi232e3CHDbRRZuYe0EnbLYdYtTi7NF9n82";
        bool use_testnet = true;  // Use demo/testnet environment
        
        if (!bybit_client_->initialize(api_key, api_secret, use_testnet)) {
            spdlog::error("[TradingEngine] Failed to initialize Bybit client");
            return false;
        }
        spdlog::info("[TradingEngine] Bybit client initialized for demo environment");
        
        // Set up callbacks for order updates and fills
        bybit_client_->set_order_update_callback(
            [this](const OrderUpdate& update) { this->on_order_update(update); });
        bybit_client_->set_fill_callback(
            [this](const FillData& fill) { this->on_fill(fill); });
        bybit_client_->set_error_callback(
            [this](const std::string& error) { this->on_exchange_error(error); });
        spdlog::info("[TradingEngine] Callbacks configured");
        
        // Connect to Bybit
        if (!bybit_client_->connect()) {
            spdlog::error("[TradingEngine] Failed to connect to Bybit");
            return false;
        }
        spdlog::info("[TradingEngine] Connected to Bybit demo environment");
        
        // Subscribe to order updates for all symbols
        if (bybit_client_->subscribe_to_orders()) {
            spdlog::info("[TradingEngine] Subscribed to order updates");
        } else {
            spdlog::warn("[TradingEngine] Failed to subscribe to order updates");
        }
        
        // Store in exchange clients map
        exchange_clients_["bybit"] = std::move(bybit_client_);
        
        spdlog::info("[TradingEngine] Live trading initialization complete");
        spdlog::info("[TradingEngine] Order endpoint: {}", order_endpoint_);
        spdlog::info("[TradingEngine] Report endpoint: {}", report_endpoint_);
        spdlog::info("[TradingEngine] Exchange clients initialized");
        
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[TradingEngine] Initialization failed: {}", e.what());
        return false;
    }
}

/**
 * @brief Enhanced start with market data threads
 */
void TradingEngineService::start() {
    if (running_) {
        spdlog::warn("[TradingEngine] Already running");
        return;
    }

    running_ = true;
    
    // Start essential threads
    order_receiver_thread_ = std::make_unique<std::thread>(&TradingEngineService::zmq_order_receiver_thread, this);
    publisher_thread_ = std::make_unique<std::thread>(&TradingEngineService::zmq_publisher_thread, this);
    
    spdlog::info("[TradingEngine] Live trading service started");
}

void TradingEngineService::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    
    // Notify publisher threads
    {
        std::lock_guard<std::mutex> lock(publish_mutex_);
        publish_cv_.notify_all();
    }
    
    // Wait for all threads
    if (order_receiver_thread_ && order_receiver_thread_->joinable()) {
        order_receiver_thread_->join();
    }
    
    if (publisher_thread_ && publisher_thread_->joinable()) {
        publisher_thread_->join();
    }
    
    spdlog::info("[TradingEngine] Live trading service stopped");
}

// ZeroMQ Communication Threads

/**
 * @brief ZeroMQ order receiver thread implementation
 * 
 * Main order processing loop that:
 * 1. Receives ExecutionOrder messages from PULL socket
 * 2. Parses JSON messages to ExecutionOrder objects
 * 3. Validates orders and checks for duplicates (idempotency)
 * 4. Dispatches orders to appropriate processing handlers
 * 
 * Runs continuously until running_ flag is set to false.
 * Uses non-blocking ZMQ_NOBLOCK to allow graceful shutdown.
 */
void TradingEngineService::zmq_order_receiver_thread() {
    spdlog::info("[TradingEngine] Order receiver thread started");
    
    while (running_) {
        try {
            zmq::message_t request;
            auto result = order_receiver_socket_->recv(request, zmq::recv_flags::dontwait);
            
            if (result.has_value()) {
                std::string message(static_cast<char*>(request.data()), request.size());
                
                // Parse ExecutionOrder from JSON
                ExecutionOrder order = parse_execution_order(message);
                
                // Process the order
                process_execution_order(order);
            }
            
            // Small sleep to prevent busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            
        } catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN) {  // EAGAIN is expected with non-blocking recv
                spdlog::error("[TradingEngine] ZeroMQ order receiver error: {}", e.what());
            }
        } catch (const std::exception& e) {
            spdlog::error("[TradingEngine] Order receiver thread error: {}", e.what());
        }
    }
    
    spdlog::info("[TradingEngine] Order receiver thread stopped");
}

/**
 * @brief ZeroMQ publisher thread implementation
 * 
 * Handles outbound message publishing:
 * 1. Waits for messages in the publish queue
 * 2. Publishes ExecutionReport and Fill messages via PUB socket
 * 3. Uses condition variable for efficient thread synchronization
 * 
 * Messages are queued by other threads and published asynchronously
 * to ensure non-blocking order processing.
 */
void TradingEngineService::zmq_publisher_thread() {
    spdlog::info("[TradingEngine] Publisher thread started");
    
    while (running_) {
        std::unique_lock<std::mutex> lock(publish_mutex_);
        
        // Wait for messages to publish or shutdown signal
        publish_cv_.wait(lock, [this] { return !publish_queue_.empty() || !running_; });
        
        while (!publish_queue_.empty()) {
            std::string topic_message = publish_queue_.front();
            publish_queue_.pop();
            
            lock.unlock();
            
            try {
                // Split topic and payload for proper ZeroMQ PUB/SUB
                size_t space_pos = topic_message.find(' ');
                if (space_pos != std::string::npos) {
                    std::string topic = topic_message.substr(0, space_pos);
                    std::string payload = topic_message.substr(space_pos + 1);
                    
                    // Send topic first with SNDMORE flag
                    zmq::message_t topic_msg(topic.size());
                    memcpy(topic_msg.data(), topic.c_str(), topic.size());
                    report_publisher_socket_->send(topic_msg, zmq::send_flags::sndmore);
                    
                    // Send payload
                    zmq::message_t payload_msg(payload.size());
                    memcpy(payload_msg.data(), payload.c_str(), payload.size());
                    report_publisher_socket_->send(payload_msg, zmq::send_flags::dontwait);
                } else {
                    // Fallback: send as single message
                    zmq::message_t zmq_msg(topic_message.size());
                    memcpy(zmq_msg.data(), topic_message.c_str(), topic_message.size());
                    report_publisher_socket_->send(zmq_msg, zmq::send_flags::dontwait);
                }
            } catch (const std::exception& e) {
                spdlog::error("[TradingEngine] Publisher error: {}", e.what());
            }
            
            lock.lock();
        }
    }
    
    spdlog::info("[TradingEngine] Publisher thread stopped");
}

/**
 * @brief Main order processing dispatcher for live trading
 * @param order The ExecutionOrder to process
 * 
 * Central routing logic that:
 * 1. Validates order structure and required fields
 * 2. Checks for duplicate orders (idempotency protection)
 * 3. Routes orders to appropriate exchange via CCAPI
 * 4. Handles errors and generates appropriate ExecutionReports
 */
void TradingEngineService::process_execution_order(const ExecutionOrder& order) {
    try {
        // Check for duplicate orders (idempotency)
        if (is_duplicate_order(order.cl_id)) {
            spdlog::warn("[TradingEngine] Ignoring duplicate order: {}", order.cl_id);
            return;
        }
        
        // Mark order as processed
        mark_order_processed(order.cl_id);
        
        // Store pending order for tracking
        pending_orders_[order.cl_id] = order;
        
        spdlog::info("[TradingEngine] Processing {} order: {}/{} cl_id={}", 
                     order.action, order.venue_type, order.product_type, order.cl_id);
        
        // Route based on action type
        if (order.action == "place") {
            if (order.venue_type == "cex") {
                if (order.product_type == "spot" || order.product_type == "perpetual") {
                    place_cex_order(order);
                } else {
                    send_rejection_report(order, "unsupported_product", 
                                        "Product type not supported: " + order.product_type);
                }
            } else {
                send_rejection_report(order, "unsupported_venue", 
                                    "Only CEX orders supported currently: " + order.venue_type);
            }
        } else if (order.action == "cancel") {
            cancel_cex_order(order);
        } else if (order.action == "replace") {
            replace_cex_order(order);
        } else {
            send_rejection_report(order, "invalid_action", "Unknown action: " + order.action);
        }
        
    } catch (const std::exception& e) {
        send_rejection_report(order, "processing_error", 
                            std::string("Processing error: ") + e.what());
        spdlog::error("[TradingEngine] Order processing error: {}", e.what());
    }
}

/**
 * @brief Place CEX order via exchange client
 * @param order The ExecutionOrder to place
 */
void TradingEngineService::place_cex_order(const ExecutionOrder& order) {
    try {
        // Get exchange client
        auto client_it = exchange_clients_.find(order.venue);
        if (client_it == exchange_clients_.end()) {
            send_rejection_report(order, "unknown_venue", 
                                "Exchange not supported: " + order.venue);
            return;
        }
        auto& client = client_it->second;
        
        // Extract order parameters
        auto symbol_it = order.details.find("symbol");
        auto side_it = order.details.find("side");
        auto size_it = order.details.find("size");
        auto order_type_it = order.details.find("order_type");
        
        if (symbol_it == order.details.end() || side_it == order.details.end() ||
            size_it == order.details.end() || order_type_it == order.details.end()) {
            send_rejection_report(order, "missing_parameters", 
                                "Missing required order parameters");
            return;
        }
        
        // Build order request
        OrderRequest req;
        req.client_order_id = order.cl_id;
        req.symbol = symbol_it->second;
        req.side = side_it->second;
        req.order_type = order_type_it->second;
        req.quantity = size_it->second;
        
        // Add price for limit orders
        if (order_type_it->second == "limit") {
            auto price_it = order.details.find("price");
            if (price_it == order.details.end()) {
                send_rejection_report(order, "missing_price", 
                                    "Price required for limit orders");
                return;
            }
            req.price = price_it->second;
        }
        
        // Set category based on product type
        if (order.product_type == "perpetual") {
            req.category = "linear";
        } else if (order.product_type == "spot") {
            req.category = "spot";
        }
        
        // Add optional time in force
        auto tif_it = order.details.find("time_in_force");
        if (tif_it != order.details.end()) {
            req.time_in_force = tif_it->second;
        } else {
            req.time_in_force = "GTC";  // Default to GTC
        }
        
        // Place order via exchange client
        OrderResponse response = client->place_order(req);
        
        if (response.success) {
            // Send acceptance report
            ExecutionReport report;
            report.cl_id = order.cl_id;
            report.status = "accepted";
            report.exchange_order_id = response.exchange_order_id;
            report.reason_code = "ok";
            report.reason_text = response.message;
            report.ts_ns = get_current_time_ns();
            report.tags = order.tags;
            
            publish_execution_report(report);
            
            spdlog::info("[TradingEngine] Order accepted: {} {} {} @ {}", 
                        side_it->second, size_it->second, symbol_it->second, 
                        order_type_it->second == "limit" ? req.price.value() : "market");
        } else {
            send_rejection_report(order, "exchange_rejected", response.message);
        }
        
    } catch (const std::exception& e) {
        send_rejection_report(order, "exchange_error", 
                            std::string("Exchange error: ") + e.what());
        spdlog::error("[TradingEngine] Order placement error: {}", e.what());
    }
}

/**
 * @brief Cancel CEX order via exchange client
 * @param order The cancel order request
 */
void TradingEngineService::cancel_cex_order(const ExecutionOrder& order) {
    try {
        // Get exchange client
        auto client_it = exchange_clients_.find(order.venue);
        if (client_it == exchange_clients_.end()) {
            send_rejection_report(order, "unknown_venue", 
                                "Exchange not supported: " + order.venue);
            return;
        }
        auto& client = client_it->second;
        
        // Get order to cancel
        auto cl_id_it = order.details.find("cl_id_to_cancel");
        if (cl_id_it == order.details.end()) {
            send_rejection_report(order, "missing_cancel_id", 
                                "Cancel orders require cl_id_to_cancel");
            return;
        }
        
        // Get optional symbol
        std::optional<std::string> symbol;
        auto symbol_it = order.details.find("symbol");
        if (symbol_it != order.details.end()) {
            symbol = symbol_it->second;
        }
        
        // Cancel order via exchange client
        OrderResponse response = client->cancel_order(cl_id_it->second, symbol);
        
        if (response.success) {
            // Send cancellation report
            ExecutionReport report;
            report.cl_id = order.cl_id;
            report.status = "cancelled";
            report.reason_code = "ok";
            report.reason_text = "Order cancelled";
            report.ts_ns = get_current_time_ns();
            report.tags = order.tags;
            
            publish_execution_report(report);
            
            spdlog::info("[TradingEngine] Order cancelled: {}", cl_id_it->second);
        } else {
            send_rejection_report(order, "cancel_rejected", response.message);
        }
        
    } catch (const std::exception& e) {
        send_rejection_report(order, "exchange_error", 
                            std::string("Exchange cancel error: ") + e.what());
        spdlog::error("[TradingEngine] Cancel error: {}", e.what());
    }
}

/**
 * @brief Replace/modify CEX order via exchange client
 * @param order The replace order request
 */
void TradingEngineService::replace_cex_order(const ExecutionOrder& order) {
    try {
        // Get exchange client
        auto client_it = exchange_clients_.find(order.venue);
        if (client_it == exchange_clients_.end()) {
            send_rejection_report(order, "unknown_venue", 
                                "Exchange not supported: " + order.venue);
            return;
        }
        auto& client = client_it->second;
        
        // Get order to replace
        auto cl_id_it = order.details.find("cl_id_to_replace");
        if (cl_id_it == order.details.end()) {
            send_rejection_report(order, "missing_replace_id", 
                                "Replace orders require cl_id_to_replace");
            return;
        }
        
        // Get new parameters
        std::optional<std::string> new_price;
        std::optional<std::string> new_quantity;
        
        auto new_price_it = order.details.find("new_price");
        if (new_price_it != order.details.end()) {
            new_price = new_price_it->second;
        }
        
        auto new_size_it = order.details.find("new_size");
        if (new_size_it != order.details.end()) {
            new_quantity = new_size_it->second;
        }
        
        // Modify order via exchange client
        OrderResponse response = client->modify_order(cl_id_it->second, new_quantity, new_price);
        
        if (response.success) {
            // Send modification report
            ExecutionReport report;
            report.cl_id = order.cl_id;
            report.status = "replaced";
            report.reason_code = "ok";
            report.reason_text = "Order modified";
            report.ts_ns = get_current_time_ns();
            report.tags = order.tags;
            
            publish_execution_report(report);
            
            spdlog::info("[TradingEngine] Order modified: {}", cl_id_it->second);
        } else {
            send_rejection_report(order, "modify_rejected", response.message);
        }
        
    } catch (const std::exception& e) {
        send_rejection_report(order, "exchange_error", 
                            std::string("Exchange modify error: ") + e.what());
        spdlog::error("[TradingEngine] Modify error: {}", e.what());
    }
}

/**
 * @brief Send rejection report for failed orders
 */
void TradingEngineService::send_rejection_report(const ExecutionOrder& order, 
                                               const std::string& reason_code,
                                               const std::string& reason_text) {
    ExecutionReport report;
    report.version = 1;
    report.cl_id = order.cl_id;
    report.status = "rejected";
    report.reason_code = reason_code;
    report.reason_text = reason_text;
    report.ts_ns = get_current_time_ns();
    report.tags = order.tags;
    
    publish_execution_report(report);
}

/**
 * @brief Callback for order updates from exchange client
 */
void TradingEngineService::on_order_update(const OrderUpdate& update) {
    try {
        // Find the original order
        auto order_it = pending_orders_.find(update.client_order_id);
        if (order_it == pending_orders_.end()) {
            spdlog::warn("[TradingEngine] Received update for unknown order: {}", 
                        update.client_order_id);
            return;
        }
        
        const ExecutionOrder& original_order = order_it->second;
        
        // Create execution report
        ExecutionReport report;
        report.version = 1;
        report.cl_id = update.client_order_id;
        report.status = update.status;
        report.exchange_order_id = update.exchange_order_id;
        report.reason_code = update.reason.empty() ? "ok" : "exchange_update";
        report.reason_text = update.reason.empty() ? 
                           "Order " + update.status : update.reason;
        report.ts_ns = get_current_time_ns();
        report.tags = original_order.tags;
        
        publish_execution_report(report);
        
        // Clean up completed orders
        if (update.status == "filled" || update.status == "cancelled" || 
            update.status == "rejected") {
            pending_orders_.erase(update.client_order_id);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("[TradingEngine] Error processing order update: {}", e.what());
    }
}

/**
 * @brief Callback for fills from exchange client
 */
void TradingEngineService::on_fill(const FillData& fill_data) {
    try {
        // Convert FillData to Fill structure
        Fill fill;
        fill.version = 1;
        fill.cl_id = fill_data.client_order_id;
        fill.exchange_order_id = fill_data.exchange_order_id;
        fill.exec_id = fill_data.exec_id;
        fill.symbol_or_pair = fill_data.symbol;
        fill.price = std::stod(fill_data.price);
        fill.size = std::stod(fill_data.quantity);
        fill.fee_currency = fill_data.fee_currency;
        fill.fee_amount = std::stod(fill_data.fee);
        fill.liquidity = fill_data.liquidity;
        fill.ts_ns = get_current_time_ns();
        
        // Copy tags from original order if available
        auto order_it = pending_orders_.find(fill.cl_id);
        if (order_it != pending_orders_.end()) {
            fill.tags = order_it->second.tags;
        }
        fill.tags["execution_type"] = "live";
        
        publish_fill(fill);
        
        spdlog::info("[TradingEngine] Fill received: {} {} @ {} (fee: {} {})", 
                     fill.cl_id, fill.size, fill.price, fill.fee_amount, fill.fee_currency);
        
    } catch (const std::exception& e) {
        spdlog::error("[TradingEngine] Error processing fill: {}", e.what());
    }
}

/**
 * @brief Callback for exchange errors
 */
void TradingEngineService::on_exchange_error(const std::string& error) {
    spdlog::error("[TradingEngine] Exchange error: {}", error);
    
    // Optionally send error notification to strategies
    // This could be expanded to send specific error reports
}

// Publishing methods
/**
 * @brief Publish ExecutionReport to strategies
 * @param report The ExecutionReport to publish
 * 
 * Thread-safe publication of execution reports:
 * 1. Serializes ExecutionReport to JSON format
 * 2. Adds message to publish queue with proper locking
 * 3. Notifies publisher thread via condition variable
 * 
 * Called by all order processing handlers to communicate
 * order status back to trading strategies.
 */
void TradingEngineService::publish_execution_report(const ExecutionReport& report) {
    try {
        std::string json_report = serialize_execution_report(report);
        std::string topic_message = "exec.report " + json_report;
        
        std::lock_guard<std::mutex> lock(publish_mutex_);
        publish_queue_.push(topic_message);
        publish_cv_.notify_one();
        
    } catch (const std::exception& e) {
        spdlog::error("[TradingEngine] Error publishing execution report: {}", e.what());
    }
}

/**
 * @brief Publish Fill report to strategies
 * @param fill The Fill report to publish
 * 
 * Thread-safe publication of fill reports:
 * 1. Serializes Fill to JSON format
 * 2. Adds message to publish queue with proper locking
 * 3. Notifies publisher thread via condition variable
 * 
 * Generated when orders are executed (partially or fully) to provide
 * detailed execution information including price, size, and fees.
 */
void TradingEngineService::publish_fill(const Fill& fill) {
    try {
        std::string json_fill = serialize_fill(fill);
        std::string topic_message = "exec.fill " + json_fill;
        
        std::lock_guard<std::mutex> lock(publish_mutex_);
        publish_queue_.push(topic_message);
        publish_cv_.notify_one();
        
    } catch (const std::exception& e) {
        spdlog::error("[TradingEngine] Error publishing fill: {}", e.what());
    }
}

// Utility methods
uint64_t TradingEngineService::get_current_time_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string TradingEngineService::generate_exec_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);
    
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    
    return "exec_" + std::to_string(millis) + "_" + std::to_string(dis(gen));
}

bool TradingEngineService::is_duplicate_order(const std::string& cl_id) {
    return processed_orders_.find(cl_id) != processed_orders_.end();
}

void TradingEngineService::mark_order_processed(const std::string& cl_id) {
    processed_orders_.insert(cl_id);
}

// Contract-compliant parsing and serialization methods
/**
 * @brief Parse JSON message to ExecutionOrder object
 * @param json_message JSON string containing ExecutionOrder data
 * @return Parsed ExecutionOrder object
 * 
 * Comprehensive JSON parsing with error handling:
 * 1. Parses base ExecutionOrder fields (cl_id, action, venue, etc.)
 * 2. Determines order detail type from venue_type and product_type
 * 3. Parses variant OrderDetails (CexOrderDetails, AmmSwapDetails, etc.)
 * 4. Validates required fields and data types
 * 5. Handles parsing errors gracefully with detailed logging
 * 
 * Central parsing logic that converts external JSON messages
 * into strongly-typed internal order structures.
 */
ExecutionOrder TradingEngineService::parse_execution_order(const std::string& json_message) {
    ExecutionOrder order;
    
    rapidjson::Document doc;
    doc.Parse(json_message.c_str());
    
    if (doc.HasParseError()) {
        throw std::runtime_error("Failed to parse ExecutionOrder JSON: " + std::string(rapidjson::GetParseError_En(doc.GetParseError())));
    }
    
    // Parse top-level fields
    if (doc.HasMember("version")) order.version = doc["version"].GetInt();
    if (doc.HasMember("cl_id")) order.cl_id = doc["cl_id"].GetString();
    if (doc.HasMember("action")) order.action = doc["action"].GetString();
    if (doc.HasMember("venue_type")) order.venue_type = doc["venue_type"].GetString();
    if (doc.HasMember("venue")) order.venue = doc["venue"].GetString();
    if (doc.HasMember("product_type")) order.product_type = doc["product_type"].GetString();
    if (doc.HasMember("ts_ns")) order.ts_ns = doc["ts_ns"].GetUint64();
    
    // Parse tags
    if (doc.HasMember("tags") && doc["tags"].IsObject()) {
        for (auto it = doc["tags"].MemberBegin(); it != doc["tags"].MemberEnd(); ++it) {
            order.tags[it->name.GetString()] = it->value.GetString();
        }
    }
    
    // Parse details based on product type and action
    if (doc.HasMember("details") && doc["details"].IsObject()) {
        const auto& details = doc["details"];
        
        if (order.action == "place") {
            if (order.venue_type == "cex" && (order.product_type == "spot" || order.product_type == "perpetual")) {
                if (details.HasMember("symbol") && details["symbol"].IsString()) order.details["symbol"] = details["symbol"].GetString();
                if (details.HasMember("side") && details["side"].IsString()) order.details["side"] = details["side"].GetString();
                if (details.HasMember("order_type") && details["order_type"].IsString()) order.details["order_type"] = details["order_type"].GetString();
                if (details.HasMember("time_in_force") && details["time_in_force"].IsString()) order.details["time_in_force"] = details["time_in_force"].GetString();
                
                // Handle numeric fields that might come as numbers or strings
                if (details.HasMember("size")) {
                    if (details["size"].IsString()) {
                        order.details["size"] = details["size"].GetString();
                    } else if (details["size"].IsNumber()) {
                        order.details["size"] = std::to_string(details["size"].GetDouble());
                    }
                }
                if (details.HasMember("price")) {
                    if (details["price"].IsString()) {
                        order.details["price"] = details["price"].GetString();
                    } else if (details["price"].IsNumber()) {
                        order.details["price"] = std::to_string(details["price"].GetDouble());
                    }
                }
                if (details.HasMember("stop_price")) {
                    if (details["stop_price"].IsString()) {
                        order.details["stop_price"] = details["stop_price"].GetString();
                    } else if (details["stop_price"].IsNumber()) {
                        order.details["stop_price"] = std::to_string(details["stop_price"].GetDouble());
                    }
                }
                
                // Handle boolean fields
                if (details.HasMember("reduce_only")) {
                    if (details["reduce_only"].IsString()) {
                        order.details["reduce_only"] = details["reduce_only"].GetString();
                    } else if (details["reduce_only"].IsBool()) {
                        order.details["reduce_only"] = details["reduce_only"].GetBool() ? "true" : "false";
                    }
                }
                
                if (details.HasMember("margin_mode") && details["margin_mode"].IsString()) order.details["margin_mode"] = details["margin_mode"].GetString();
                
                // Parse params
                if (details.HasMember("params") && details["params"].IsObject()) {
                    for (auto it = details["params"].MemberBegin(); it != details["params"].MemberEnd(); ++it) {
                        if (it->value.IsString()) {
                            order.details[it->name.GetString()] = it->value.GetString();
                        } else if (it->value.IsNumber()) {
                            order.details[it->name.GetString()] = std::to_string(it->value.GetDouble());
                        } else if (it->value.IsBool()) {
                            order.details[it->name.GetString()] = it->value.GetBool() ? "true" : "false";
                        }
                    }
                }
                
            } else if (order.venue_type == "dex" && order.product_type == "amm_swap") {
                if (details.HasMember("chain")) order.details["chain"] = details["chain"].GetString();
                if (details.HasMember("protocol")) order.details["protocol"] = details["protocol"].GetString();
                if (details.HasMember("token_in")) order.details["token_in"] = details["token_in"].GetString();
                if (details.HasMember("token_out")) order.details["token_out"] = details["token_out"].GetString();
                if (details.HasMember("trade_mode")) order.details["trade_mode"] = details["trade_mode"].GetString();
                if (details.HasMember("amount_in")) order.details["amount_in"] = details["amount_in"].GetString();
                if (details.HasMember("amount_out")) order.details["amount_out"] = details["amount_out"].GetString();
                if (details.HasMember("slippage_bps")) order.details["slippage_bps"] = details["slippage_bps"].GetString();
                if (details.HasMember("deadline_sec")) order.details["deadline_sec"] = details["deadline_sec"].GetString();
                if (details.HasMember("recipient")) order.details["recipient"] = details["recipient"].GetString();
                
                // Parse route
                if (details.HasMember("route") && details["route"].IsArray()) {
                    std::string route_str = "";
                    for (const auto& v : details["route"].GetArray()) {
                        if (!route_str.empty()) route_str += ",";
                        route_str += v.GetString();
                    }
                    order.details["route"] = route_str;
                }
                
            } else if (order.venue_type == "dex" && order.product_type == "clmm_swap") {
                if (details.HasMember("chain")) order.details["chain"] = details["chain"].GetString();
                if (details.HasMember("protocol")) order.details["protocol"] = details["protocol"].GetString();
                if (details.HasMember("trade_mode")) order.details["trade_mode"] = details["trade_mode"].GetString();
                if (details.HasMember("amount_in")) order.details["amount_in"] = details["amount_in"].GetString();
                if (details.HasMember("amount_out")) order.details["amount_out"] = details["amount_out"].GetString();
                if (details.HasMember("slippage_bps")) order.details["slippage_bps"] = details["slippage_bps"].GetString();
                if (details.HasMember("price_limit")) order.details["price_limit"] = details["price_limit"].GetString();
                if (details.HasMember("deadline_sec")) order.details["deadline_sec"] = details["deadline_sec"].GetString();
                if (details.HasMember("recipient")) order.details["recipient"] = details["recipient"].GetString();
                
                // Parse pool details
                if (details.HasMember("pool") && details["pool"].IsObject()) {
                    const auto& pool = details["pool"];
                    if (pool.HasMember("token0")) order.details["pool_token0"] = pool["token0"].GetString();
                    if (pool.HasMember("token1")) order.details["pool_token1"] = pool["token1"].GetString();
                    if (pool.HasMember("fee_tier_bps")) order.details["pool_fee_tier_bps"] = pool["fee_tier_bps"].GetString();
                }
                
            } else if (order.venue_type == "chain" && order.product_type == "transfer") {
                if (details.HasMember("chain")) order.details["chain"] = details["chain"].GetString();
                if (details.HasMember("token")) order.details["token"] = details["token"].GetString();
                if (details.HasMember("amount")) order.details["amount"] = details["amount"].GetString();
                if (details.HasMember("to_address")) order.details["to_address"] = details["to_address"].GetString();
            }
        } else if (order.action == "cancel") {
            if (details.HasMember("symbol")) order.details["symbol"] = details["symbol"].GetString();
            if (details.HasMember("cl_id_to_cancel")) order.details["cl_id_to_cancel"] = details["cl_id_to_cancel"].GetString();
            if (details.HasMember("exchange_order_id")) order.details["exchange_order_id"] = details["exchange_order_id"].GetString();
            
        } else if (order.action == "replace") {
            if (details.HasMember("symbol")) order.details["symbol"] = details["symbol"].GetString();
            if (details.HasMember("cl_id_to_replace")) order.details["cl_id_to_replace"] = details["cl_id_to_replace"].GetString();
            if (details.HasMember("new_price")) order.details["new_price"] = details["new_price"].GetString();
            if (details.HasMember("new_size")) order.details["new_size"] = details["new_size"].GetString();
        }
    }
    
    return order;
}

std::string TradingEngineService::serialize_execution_report(const ExecutionReport& report) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();
    
    doc.AddMember("version", rapidjson::Value(report.version), allocator);
    doc.AddMember("cl_id", rapidjson::Value(report.cl_id.c_str(), allocator), allocator);
    doc.AddMember("status", rapidjson::Value(report.status.c_str(), allocator), allocator);
    
    if (report.exchange_order_id.has_value()) {
        doc.AddMember("exchange_order_id", rapidjson::Value(report.exchange_order_id.value().c_str(), allocator), allocator);
    } else {
        doc.AddMember("exchange_order_id", rapidjson::Value().SetNull(), allocator);
    }
    
    doc.AddMember("reason_code", rapidjson::Value(report.reason_code.c_str(), allocator), allocator);
    doc.AddMember("reason_text", rapidjson::Value(report.reason_text.c_str(), allocator), allocator);
    doc.AddMember("ts_ns", rapidjson::Value(report.ts_ns), allocator);
    
    // Add tags
    rapidjson::Value tags_obj(rapidjson::kObjectType);
    for (const auto& tag : report.tags) {
        tags_obj.AddMember(
            rapidjson::Value(tag.first.c_str(), allocator),
            rapidjson::Value(tag.second.c_str(), allocator),
            allocator
        );
    }
    doc.AddMember("tags", tags_obj, allocator);
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    return buffer.GetString();
}

std::string TradingEngineService::serialize_fill(const Fill& fill) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();
    
    doc.AddMember("version", rapidjson::Value(fill.version), allocator);
    doc.AddMember("cl_id", rapidjson::Value(fill.cl_id.c_str(), allocator), allocator);
    
    if (fill.exchange_order_id.has_value()) {
        doc.AddMember("exchange_order_id", rapidjson::Value(fill.exchange_order_id.value().c_str(), allocator), allocator);
    } else {
        doc.AddMember("exchange_order_id", rapidjson::Value().SetNull(), allocator);
    }
    
    doc.AddMember("exec_id", rapidjson::Value(fill.exec_id.c_str(), allocator), allocator);
    doc.AddMember("symbol_or_pair", rapidjson::Value(fill.symbol_or_pair.c_str(), allocator), allocator);
    doc.AddMember("price", rapidjson::Value(fill.price), allocator);
    doc.AddMember("size", rapidjson::Value(fill.size), allocator);
    doc.AddMember("fee_currency", rapidjson::Value(fill.fee_currency.c_str(), allocator), allocator);
    doc.AddMember("fee_amount", rapidjson::Value(fill.fee_amount), allocator);
    
    if (fill.liquidity.has_value()) {
        doc.AddMember("liquidity", rapidjson::Value(fill.liquidity.value().c_str(), allocator), allocator);
    } else {
        doc.AddMember("liquidity", rapidjson::Value().SetNull(), allocator);
    }
    
    doc.AddMember("ts_ns", rapidjson::Value(fill.ts_ns), allocator);
    
    // Add tags
    rapidjson::Value tags_obj(rapidjson::kObjectType);
    for (const auto& tag : fill.tags) {
        tags_obj.AddMember(
            rapidjson::Value(tag.first.c_str(), allocator),
            rapidjson::Value(tag.second.c_str(), allocator),
            allocator
        );
    }
    doc.AddMember("tags", tags_obj, allocator);
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    return buffer.GetString();
}

/**
 * @brief Get exchanges from environment variable or default
 * @return Vector of exchange names
 */
std::vector<std::string> TradingEngineService::getExchangesFromConfig() const {
    // No-op in simplified version
    return {};
}

/**
 * @brief Get symbols from environment variable or default
 * @return Vector of symbol names
 */
std::vector<std::string> TradingEngineService::getSymbolsFromConfig() const {
    // No-op in simplified version
    return {};
}

/**
 * @brief Get symbols dynamically from exchange APIs
 */
std::vector<std::string> TradingEngineService::getDynamicSymbolsFromExchange(
    const std::string& exchange_name,
    int top_n,
    const std::string& quote_currency) const {
    
    // No-op in simplified version
    return {};
}

/**
 * @brief Parse comma-separated string into vector
 * @param input Comma-separated string
 * @return Vector of strings
 */
std::vector<std::string> TradingEngineService::parseCommaSeparated(const std::string& input) const {
    // No-op in simplified version
    return {};
}

} // namespace latentspeed
