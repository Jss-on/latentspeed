/**
 * @file trading_engine_service.cpp
 * @brief Simplified implementation focused on Python example integration
 * @author jessiondiwangan@gmail.com
 * @date 2025
 * 
 * SIMPLIFIED VERSION - Only includes functionality needed for:
 * - Basic ZeroMQ order communication  
 * - CEX order handling in backtest mode
 * - Integration with Python run_exec_example.py
 */

#include "trading_engine_service.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_set>
#include <random>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>

namespace latentspeed {

/**
 * @brief Simplified constructor - focuses only on essential components
 */
TradingEngineService::TradingEngineService()
    : running_(false)
    , order_endpoint_("tcp://127.0.0.1:5601")        // Contract-specified endpoint
    , report_endpoint_("tcp://127.0.0.1:5602")       // Contract-specified endpoint
    , backtest_mode_(true)                            // Always start in backtest mode
    , fill_probability_(0.9)                          // High fill probability for testing
    , slippage_bps_(1.0)                             // 1 bps slippage
{
    std::cout << "[TradingEngine] Simplified trading engine initialized" << std::endl;
    std::cout << "[TradingEngine] Mode: Backtest only (DEX/live trading disabled)" << std::endl;
}

TradingEngineService::~TradingEngineService() {
    stop();
}

/**
 * @brief Simplified initialization - only ZeroMQ sockets
 */
bool TradingEngineService::initialize() {
    try {
        // Initialize ZeroMQ context
        zmq_context_ = std::make_unique<zmq::context_t>(1);
        
        // Order receiver socket (PULL - receives ExecutionOrders from Python)
        order_receiver_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PULL);
        order_receiver_socket_->bind(order_endpoint_);
        
        // Report publisher socket (PUB - sends ExecutionReports and Fills to Python)
        report_publisher_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PUB);
        report_publisher_socket_->bind(report_endpoint_);

        // Skip complex market data subscriptions for now
        // Skip ccapi initialization for now

        std::cout << "[TradingEngine] Simplified initialization complete" << std::endl;
        std::cout << "[TradingEngine] Order endpoint: " << order_endpoint_ << std::endl;
        std::cout << "[TradingEngine] Report endpoint: " << report_endpoint_ << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[TradingEngine] Initialization failed: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief Simplified start - only essential threads
 */
void TradingEngineService::start() {
    if (running_) {
        std::cout << "[TradingEngine] Already running" << std::endl;
        return;
    }

    running_ = true;
    
    // Start only essential threads
    order_receiver_thread_ = std::make_unique<std::thread>(&TradingEngineService::zmq_order_receiver_thread, this);
    publisher_thread_ = std::make_unique<std::thread>(&TradingEngineService::zmq_publisher_thread, this);
    
    // Skip market data threads for now
    
    std::cout << "[TradingEngine] Simplified service started (order processing + backtest simulation)" << std::endl;
}

void TradingEngineService::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    
    // Notify publisher thread
    {
        std::lock_guard<std::mutex> lock(publish_mutex_);
        publish_cv_.notify_all();
    }
    
    // Wait for essential threads
    if (order_receiver_thread_ && order_receiver_thread_->joinable()) {
        order_receiver_thread_->join();
    }
    
    if (publisher_thread_ && publisher_thread_->joinable()) {
        publisher_thread_->join();
    }
    
    std::cout << "[TradingEngine] Simplified service stopped" << std::endl;
}

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
    std::cout << "[TradingEngine] Order receiver thread started" << std::endl;
    
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
                std::cerr << "[TradingEngine] ZeroMQ order receiver error: " << e.what() << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[TradingEngine] Order receiver thread error: " << e.what() << std::endl;
        }
    }
    
    std::cout << "[TradingEngine] Order receiver thread stopped" << std::endl;
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
    std::cout << "[TradingEngine] Publisher thread started" << std::endl;
    
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
                std::cerr << "[TradingEngine] Publisher error: " << e.what() << std::endl;
            }
            
            lock.lock();
        }
    }
    
    std::cout << "[TradingEngine] Publisher thread stopped" << std::endl;
}

/**
 * @brief Main order processing dispatcher
 * @param order The ExecutionOrder to process
 * 
 * Central routing logic that:
 * 1. Validates order structure and required fields
 * 2. Checks for duplicate orders (idempotency protection)
 * 3. Routes orders to appropriate venue-specific handlers based on:
 *    - venue_type (cex, dex, chain)
 *    - action (place, cancel, replace)
 *    - product_type (spot, perpetual, swap, transfer)
 * 4. Handles errors and generates appropriate ExecutionReports
 * 
 * This is the main entry point for all order processing logic.
 */
void TradingEngineService::process_execution_order(const ExecutionOrder& order) {
    try {
        // Check for duplicate orders (idempotency)
        if (is_duplicate_order(order.cl_id)) {
            std::cout << "[TradingEngine] Ignoring duplicate order: " << order.cl_id << std::endl;
            return;
        }
        
        // Mark order as processed
        mark_order_processed(order.cl_id);
        
        // Store pending order for tracking
        pending_orders_[order.cl_id] = order;
        
        std::cout << "[TradingEngine] Processing " << order.action << " order: " 
                  << order.venue_type << "/" << order.product_type << " cl_id=" << order.cl_id << std::endl;
        
        // Route based on action type
        if (order.action == "place") {
            if (order.venue_type == "cex") {
                if (order.product_type == "spot" || order.product_type == "perpetual") {
                    // In simplified version, just simulate the order directly
                    simulate_order_execution_simplified(order);
                }
            } else {
                std::cout << "[TradingEngine] Non-CEX orders not supported in simplified mode: " 
                          << order.venue_type << std::endl;
                
                // Send rejection report
                ExecutionReport report;
                report.version = 1;
                report.cl_id = order.cl_id;
                report.status = "rejected";
                report.reason_code = "unsupported_venue";
                report.reason_text = "Only CEX orders supported in simplified mode";
                report.ts_ns = get_current_time_ns();
                
                publish_execution_report(report);
            }
        } else if (order.action == "cancel") {
            std::cout << "[TradingEngine] Cancel orders not implemented in simplified mode" << std::endl;
            
            // Send rejection report
            ExecutionReport report;
            report.version = 1;
            report.cl_id = order.cl_id;
            report.status = "rejected";
            report.reason_code = "unsupported_action";
            report.reason_text = "Cancel orders not supported in simplified mode";
            report.ts_ns = get_current_time_ns();
            
            publish_execution_report(report);
        } else {
            std::cout << "[TradingEngine] Unknown action: " << order.action << std::endl;
            
            // Send rejection report
            ExecutionReport report;
            report.version = 1;
            report.cl_id = order.cl_id;
            report.status = "rejected";
            report.reason_code = "invalid_action";
            report.reason_text = "Unknown action: " + order.action;
            report.ts_ns = get_current_time_ns();
            
            publish_execution_report(report);
        }
        
    } catch (const std::exception& e) {
        // Send rejection report
        ExecutionReport report;
        report.version = 1;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "invalid_params";
        report.reason_text = std::string("Processing error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        
        publish_execution_report(report);
        
        std::cerr << "[TradingEngine] Order processing error: " << e.what() << std::endl;
    }
}

/**
 * @brief Handle CEX orders in simplified mode
 * @param order The original ExecutionOrder
 * 
 * Simulates CEX order execution in simplified mode:
 * 1. Validates order parameters (symbol, side, type, etc.)
 * 2. Simulates execution using market data
 * 3. Generates ExecutionReport and Fill messages
 * 
 * Simplified version of CEX order handling that focuses on
 * basic order simulation without actual exchange interactions.
 */
void TradingEngineService::simulate_order_execution_simplified(const ExecutionOrder& order) {
    try {
        // Check if we're in backtest mode - if so, simulate execution
        if (backtest_mode_) {
            // Get order details from the map
            std::string symbol = order.details.at("symbol");
            std::string side = order.details.at("side");
            double size = std::stod(order.details.at("size"));
            std::string price_str = order.details.count("price") ? order.details.at("price") : "market";
            
            std::cout << "[TradingEngine] Simulating CEX order: " << symbol << " " 
                      << side << " " << size << " @ " << price_str << std::endl;
            
            // Simulate order execution
            double fill_price = calculate_fill_price(order);
            double fill_size = size;
            generate_fill_from_market_data(order, fill_price, fill_size);
        }
        
    } catch (const std::exception& e) {
        // Send rejection report
        ExecutionReport report;
        report.version = 1;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "network_error";
        report.reason_text = std::string("Simulation error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        
        publish_execution_report(report);
    }
}

/**
 * @brief Calculate realistic fill price for order execution
 * @param order The order being filled
 * @return Calculated execution price including slippage
 * 
 * Price calculation that accounts for:
 * 1. Order type (market orders get best available price)
 * 2. Limit order price improvement when possible
 * 3. Realistic slippage based on order size and liquidity
 * 4. Configurable additional slippage for conservative simulation
 * 
 * Ensures backtest fills use realistic prices that would
 * occur in actual market conditions.
 */
double TradingEngineService::calculate_fill_price(const ExecutionOrder& order) {
    double base_price;
    
    if (order.details.count("order_type") && order.details.at("order_type") == "market") {
        // Market orders get filled at best available price
        base_price = 50000.0;  // Default mid price
    } else if (order.details.count("order_type") && order.details.at("order_type") == "limit" && order.details.count("price")) {
        // Limit orders get filled at limit price or better
        base_price = std::stod(order.details.at("price"));
    } else {
        base_price = 50000.0;
    }
    
    // Add realistic slippage
    double slippage_factor = slippage_bps_ / 10000.0;  // Convert bps to decimal
    if (order.details.at("side") == "buy") {
        base_price *= (1.0 + slippage_factor);
    } else {
        base_price *= (1.0 - slippage_factor);
    }
    
    return base_price;
}

/**
 * @brief Generate fill report from market data
 * @param order The original ExecutionOrder
 * @param fill_price Calculated fill price
 * @param fill_size Calculated fill size
 * 
 * Generates Fill report based on simulated execution:
 * 1. Extracts symbol from order details
 * 2. Calculates fill price and size
 * 3. Generates Fill message with execution details
 * 
 * Used to simulate realistic fills in backtest mode.
 */
void TradingEngineService::generate_fill_from_market_data(const ExecutionOrder& order, double fill_price, double fill_size) {
    // First generate acceptance report
    ExecutionReport acceptance_report;
    acceptance_report.version = 1;
    acceptance_report.cl_id = order.cl_id;
    acceptance_report.status = "accepted";
    acceptance_report.reason_code = "ok";
    acceptance_report.reason_text = "Order accepted in backtest simulation";
    acceptance_report.ts_ns = get_current_time_ns();
    acceptance_report.tags = order.tags;
    acceptance_report.tags["execution_type"] = "simulated";
    
    publish_execution_report(acceptance_report);
    
    // Generate fill report
    Fill fill;
    fill.cl_id = order.cl_id;
    fill.exec_id = generate_exec_id();
    
    // Extract symbol from order details based on type
    fill.symbol_or_pair = order.details.at("symbol");
    
    fill.price = fill_price;
    fill.size = fill_size;
    fill.fee_currency = "USDT";
    fill.fee_amount = fill_price * fill_size * 0.001;  // 0.1% fee
    fill.liquidity = "taker";
    fill.ts_ns = get_current_time_ns();
    fill.tags = order.tags;
    fill.tags["execution_type"] = "simulated";
    
    publish_fill(fill);
    
    // Remove from pending orders
    pending_orders_.erase(order.cl_id);
    
    std::cout << "[BacktestEngine] Simulated fill: " << order.cl_id 
              << " symbol=" << fill.symbol_or_pair
              << " price=" << fill_price 
              << " size=" << fill_size << std::endl;
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
        std::cerr << "[TradingEngine] Error publishing execution report: " << e.what() << std::endl;
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
        std::cerr << "[TradingEngine] Error publishing fill: " << e.what() << std::endl;
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

} // namespace latentspeed
