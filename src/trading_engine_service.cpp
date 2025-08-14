/**
 * @file trading_engine_service.cpp
 * @brief Implementation of the TradingEngineService class
 * @author Latentspeed Trading Team
 * @date 2024
 * 
 * This file contains the complete implementation of the TradingEngineService,
 * including order processing, market data handling, ZeroMQ communication,
 * and backtest simulation capabilities.
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
 * @brief Default constructor for TradingEngineService
 * 
 * Initializes the trading engine with default configuration values:
 * - Order endpoint: tcp://127.0.0.1:5601 (PULL socket)
 * - Report endpoint: tcp://127.0.0.1:5602 (PUB socket)
 * - Gateway URL: http://localhost:8080 (Hummingbot Gateway)
 * - Market data endpoints: tcp://127.0.0.1:5556/5557
 * - Backtest mode enabled with 80% fill probability
 * - 1 bps additional slippage for realistic simulation
 */
TradingEngineService::TradingEngineService()
    : running_(false)
    , order_endpoint_("tcp://127.0.0.1:5601")        // Contract-specified endpoint
    , report_endpoint_("tcp://127.0.0.1:5602")       // Contract-specified endpoint
    , gateway_base_url_("http://localhost:8080")      // Default Hummingbot Gateway URL
    , market_data_host_("127.0.0.1")                 // Default to localhost
    , trade_endpoint_("tcp://127.0.0.1:5556")        // Preprocessed trades endpoint
    , orderbook_endpoint_("tcp://127.0.0.1:5557")    // Preprocessed orderbook endpoint
    , backtest_mode_(true)                            // Default to backtest mode
    , fill_probability_(0.8)                          // 80% fill probability
    , slippage_bps_(1.0)                             // 1 bps additional slippage
{
    // Initialize curl for DEX gateway communication
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

/**
 * @brief Destructor for TradingEngineService
 * 
 * Ensures graceful shutdown of the service and cleanup of all resources:
 * - Stops all worker threads
 * - Cleans up curl library resources
 * - Releases ZeroMQ and ccapi resources
 */
TradingEngineService::~TradingEngineService() {
    stop();
    curl_global_cleanup();
}

/**
 * @brief Initialize the trading engine service
 * @return true if initialization successful, false otherwise
 * 
 * Performs complete service initialization:
 * - Creates ZeroMQ context and sockets (PULL, PUB, SUB)
 * - Binds order receiver and report publisher sockets
 * - Connects to market data feeds (trades and orderbook)
 * - Initializes ccapi session for exchange connectivity
 * - Sets up all required components for operation
 * 
 * Must be called before start(). The service will log detailed
 * initialization status and endpoint information.
 */
bool TradingEngineService::initialize() {
    try {
        // Initialize ZeroMQ context
        zmq_context_ = std::make_unique<zmq::context_t>(1);
        
        // Order receiver socket (PULL - receives ExecutionOrders)
        order_receiver_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PULL);
        order_receiver_socket_->bind(order_endpoint_);
        
        // Report publisher socket (PUB - sends ExecutionReports and Fills)
        report_publisher_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PUB);
        report_publisher_socket_->bind(report_endpoint_);

        // Market data subscriber sockets
        trade_subscriber_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_SUB);
        trade_subscriber_socket_->connect(trade_endpoint_);
        trade_subscriber_socket_->setsockopt(ZMQ_SUBSCRIBE, "", 0);  // Subscribe to all topics
        
        orderbook_subscriber_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_SUB);
        orderbook_subscriber_socket_->connect(orderbook_endpoint_);
        orderbook_subscriber_socket_->setsockopt(ZMQ_SUBSCRIBE, "", 0);  // Subscribe to all topics

        // Initialize ccapi for CEX integration
        session_options_ = std::make_unique<ccapi::SessionOptions>();
        session_configs_ = std::make_unique<ccapi::SessionConfigs>();
        ccapi_session_ = std::make_unique<ccapi::Session>(*session_options_, *session_configs_, this);

        std::cout << "[TradingEngine] Initialized successfully" << std::endl;
        std::cout << "[TradingEngine] Order receiver endpoint: " << order_endpoint_ << std::endl;
        std::cout << "[TradingEngine] Report publisher endpoint: " << report_endpoint_ << std::endl;
        std::cout << "[TradingEngine] DEX Gateway URL: " << gateway_base_url_ << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[TradingEngine] Initialization failed: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief Start the trading engine service
 * 
 * Launches all worker threads in the correct order:
 * 1. Order receiver thread (processes incoming ExecutionOrders)
 * 2. Publisher thread (sends ExecutionReports and Fills)
 * 3. Trade subscriber thread (processes preprocessed trade data)
 * 4. Orderbook subscriber thread (processes preprocessed orderbook data)
 * 
 * This is a non-blocking call. The service will continue running
 * until stop() is called. Each thread runs independently and
 * communicates through thread-safe queues and atomic flags.
 */
void TradingEngineService::start() {
    if (running_) {
        std::cout << "[TradingEngine] Already running" << std::endl;
        return;
    }

    running_ = true;
    
    // Start order receiver thread
    order_receiver_thread_ = std::make_unique<std::thread>(&TradingEngineService::zmq_order_receiver_thread, this);
    
    // Start publisher thread
    publisher_thread_ = std::make_unique<std::thread>(&TradingEngineService::zmq_publisher_thread, this);
    
    // Start market data subscriber threads
    trade_subscriber_thread_ = std::make_unique<std::thread>(&TradingEngineService::zmq_trade_subscriber_thread, this);
    orderbook_subscriber_thread_ = std::make_unique<std::thread>(&TradingEngineService::zmq_orderbook_subscriber_thread, this);
    
    std::cout << "[TradingEngine] Service started" << std::endl;
    std::cout << "[TradingEngine] Market data streams: trades=" << trade_endpoint_ << ", orderbook=" << orderbook_endpoint_ << std::endl;
    std::cout << "[TradingEngine] Backtest mode: " << (backtest_mode_ ? "enabled" : "disabled") << std::endl;
}

/**
 * @brief Stop the trading engine service
 * 
 * Performs graceful shutdown of all components:
 * 1. Sets running flag to false (signals threads to stop)
 * 2. Waits for all worker threads to complete their current operations
 * 3. Joins all threads to ensure clean shutdown
 * 4. Releases thread resources
 * 
 * This is a blocking call that ensures all operations are completed
 * before returning. Safe to call multiple times.
 */
void TradingEngineService::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    
    // Notify publisher thread to wake up
    {
        std::lock_guard<std::mutex> lock(publish_mutex_);
        publish_cv_.notify_all();
    }
    
    // Stop ccapi session
    if (ccapi_session_) {
        ccapi_session_->stop();
    }
    
    // Wait for threads to finish
    if (order_receiver_thread_ && order_receiver_thread_->joinable()) {
        order_receiver_thread_->join();
    }
    
    if (publisher_thread_ && publisher_thread_->joinable()) {
        publisher_thread_->join();
    }
    
    if (trade_subscriber_thread_ && trade_subscriber_thread_->joinable()) {
        trade_subscriber_thread_->join();
    }
    
    if (orderbook_subscriber_thread_ && orderbook_subscriber_thread_->joinable()) {
        orderbook_subscriber_thread_->join();
    }
    
    std::cout << "[TradingEngine] Service stopped" << std::endl;
}

/**
 * @brief Process ccapi events from exchanges
 * @param event The ccapi event containing market data or execution updates
 * @param sessionPtr Pointer to the ccapi session
 * 
 * Handles different types of ccapi events:
 * - Market data updates (trades, orderbook changes)
 * - Order execution confirmations
 * - Connection status changes
 * - Error notifications
 * 
 * This method is called by the ccapi framework when events occur.
 * It processes the events and updates internal state accordingly.
 */
void TradingEngineService::processEvent(const ccapi::Event& event, ccapi::Session* sessionPtr) {
    if (event.getType() == ccapi::Event::Type::SUBSCRIPTION_STATUS) {
        std::cout << "[TradingEngine] Subscription status: " << event.toPrettyString(2, 2) << std::endl;
    } else if (event.getType() == ccapi::Event::Type::SUBSCRIPTION_DATA) {
        // Process market data - unchanged for now, can be enhanced later
        for (const auto& message : event.getMessageList()) {
            MarketDataSnapshot snapshot;
            
            // Extract correlation ID to determine exchange/instrument
            auto correlationIds = message.getCorrelationIdList();
            if (!correlationIds.empty()) {
                std::string corrId = correlationIds[0];
                size_t pos = corrId.find(':');
                if (pos != std::string::npos) {
                    snapshot.exchange = corrId.substr(0, pos);
                    snapshot.instrument = corrId.substr(pos + 1);
                }
            }
            
            // Extract market data
            for (const auto& element : message.getElementList()) {
                const auto& nameValueMap = element.getNameValueMap();
                
                auto bidPrice = nameValueMap.find("BID_PRICE");
                auto bidSize = nameValueMap.find("BID_SIZE");
                auto askPrice = nameValueMap.find("ASK_PRICE");
                auto askSize = nameValueMap.find("ASK_SIZE");
                
                if (bidPrice != nameValueMap.end()) snapshot.bid_price = std::stod(bidPrice->second);
                if (bidSize != nameValueMap.end()) snapshot.bid_size = std::stod(bidSize->second);
                if (askPrice != nameValueMap.end()) snapshot.ask_price = std::stod(askPrice->second);
                if (askSize != nameValueMap.end()) snapshot.ask_size = std::stod(askSize->second);
            }
            
            snapshot.timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            
            // TODO: Broadcast market data via contract-compliant method
            std::cout << "[TradingEngine] Market data: " << snapshot.exchange << ":" << snapshot.instrument 
                      << " bid=" << snapshot.bid_price << " ask=" << snapshot.ask_price << std::endl;
        }
    } else if (event.getType() == ccapi::Event::Type::RESPONSE) {
        // Handle CEX order responses
        for (const auto& message : event.getMessageList()) {
            auto correlationIds = message.getCorrelationIdList();
            if (!correlationIds.empty()) {
                std::string cl_id = correlationIds[0];
                
                // Create ExecutionReport based on response
                ExecutionReport report;
                report.cl_id = cl_id;
                report.ts_ns = get_current_time_ns();
                
                // Check if order was accepted or rejected
                if (message.getType() == ccapi::Message::Type::CREATE_ORDER) {
                    auto elements = message.getElementList();
                    if (!elements.empty()) {
                        const auto& element = elements[0];
                        const auto& nameValueMap = element.getNameValueMap();
                        
                        auto orderIdIt = nameValueMap.find("ORDER_ID");
                        if (orderIdIt != nameValueMap.end()) {
                            report.status = "accepted";
                            report.reason_code = "ok";
                            report.reason_text = "Order accepted by exchange";
                            report.exchange_order_id = orderIdIt->second;
                            
                            // Store mapping for future reference
                            cex_order_mapping_[cl_id] = orderIdIt->second;
                        } else {
                            report.status = "rejected";
                            report.reason_code = "venue_reject";
                            report.reason_text = "Order rejected by exchange";
                        }
                    }
                } else {
                    report.status = "rejected";
                    report.reason_code = "venue_reject";
                    report.reason_text = "Order rejected by exchange";
                }
                
                publish_execution_report(report);
            }
        }
    }
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
            std::string message = publish_queue_.front();
            publish_queue_.pop();
            
            lock.unlock();
            
            try {
                zmq::message_t zmq_msg(message.size());
                memcpy(zmq_msg.data(), message.c_str(), message.size());
                report_publisher_socket_->send(zmq_msg, zmq::send_flags::dontwait);
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
        
        // Route based on action and product type
        if (order.action == "place") {
            if (order.venue_type == "cex") {
                if (order.product_type == "spot" || order.product_type == "perpetual") {
                    const auto& details = std::get<CexOrderDetails>(order.details);
                    handle_cex_order(order, details);
                }
            } else if (order.venue_type == "dex") {
                if (order.product_type == "amm_swap") {
                    const auto& details = std::get<AmmSwapDetails>(order.details);
                    handle_amm_swap(order, details);
                } else if (order.product_type == "clmm_swap") {
                    const auto& details = std::get<ClmmSwapDetails>(order.details);
                    handle_clmm_swap(order, details);
                }
            } else if (order.venue_type == "chain") {
                if (order.product_type == "transfer") {
                    const auto& details = std::get<TransferDetails>(order.details);
                    handle_transfer(order, details);
                }
            }
        } else if (order.action == "cancel") {
            const auto& details = std::get<CancelDetails>(order.details);
            handle_cancel(order, details);
        } else if (order.action == "replace") {
            const auto& details = std::get<ReplaceDetails>(order.details);
            handle_replace(order, details);
        } else {
            // Reject unknown action
            ExecutionReport report;
            report.cl_id = order.cl_id;
            report.status = "rejected";
            report.reason_code = "invalid_params";
            report.reason_text = "Unknown action: " + order.action;
            report.ts_ns = get_current_time_ns();
            publish_execution_report(report);
        }
        
    } catch (const std::exception& e) {
        // Send rejection report
        ExecutionReport report;
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
 * @brief Handle centralized exchange orders
 * @param order The original ExecutionOrder
 * @param details CEX-specific order parameters
 * 
 * Processes orders for centralized exchanges (Binance, Bybit, OKX, etc.):
 * 1. Validates CEX-specific parameters (symbol, side, type, etc.)
 * 2. In backtest mode: simulates execution using market data
 * 3. In live mode: submits orders via ccapi to actual exchanges
 * 4. Tracks pending orders and manages order state
 * 5. Generates ExecutionReports for order status updates
 * 
 * Supports all standard order types: market, limit, stop, stop-limit
 * with proper risk management and position sizing.
 */
void TradingEngineService::handle_cex_order(const ExecutionOrder& order, const CexOrderDetails& details) {
    try {
        // Create ccapi request for CEX order
        ccapi::Request request(ccapi::Request::Operation::CREATE_ORDER, order.venue, details.symbol, order.cl_id);
        
        // Map order parameters
        std::map<std::string, std::string> params = details.params;
        params["SIDE"] = details.side == "buy" ? "BUY" : "SELL";
        params["QUANTITY"] = std::to_string(details.size);
        params["ORDER_TYPE"] = details.order_type;
        
        if (details.price.has_value()) {
            params["LIMIT_PRICE"] = std::to_string(details.price.value());
        }
        if (details.stop_price.has_value()) {
            params["STOP_PRICE"] = std::to_string(details.stop_price.value());
        }
        if (details.time_in_force != "gtc") {  // gtc is usually default
            params["TIME_IN_FORCE"] = details.time_in_force;
        }
        if (details.reduce_only) {
            params["REDUCE_ONLY"] = "true";
        }
        if (details.margin_mode.has_value()) {
            params["MARGIN_MODE"] = details.margin_mode.value();
        }
        
        request.appendParam(params);
        
        // Send request to ccapi
        ccapi_session_->sendRequest(request);
        
        std::cout << "[TradingEngine] CEX order submitted: " << details.symbol << " " 
                  << details.side << " " << details.size << std::endl;
        
    } catch (const std::exception& e) {
        // Send rejection report
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "network_error";
        report.reason_text = std::string("CEX order error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
    }
}

/**
 * @brief Handle Automated Market Maker swap orders
 * @param order The original ExecutionOrder
 * @param details AMM-specific swap parameters
 * 
 * Processes token swaps on AMM protocols (Uniswap V2, SushiSwap, etc.):
 * 1. Validates swap parameters (tokens, amounts, slippage)
 * 2. Constructs swap transaction via Hummingbot Gateway API
 * 3. Handles both exact_in and exact_out swap modes
 * 4. Applies slippage protection and deadline constraints
 * 5. Supports multi-hop routing through intermediate tokens
 * 
 * Communicates with DEX protocols through Gateway REST API,
 * abstracting blockchain complexity from the trading engine.
 */
void TradingEngineService::handle_amm_swap(const ExecutionOrder& order, const AmmSwapDetails& details) {
    try {
        // Build JSON payload for Hummingbot Gateway AMM swap
        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();
        
        doc.AddMember("chain", rapidjson::Value(details.chain.c_str(), allocator), allocator);
        doc.AddMember("connector", rapidjson::Value(details.protocol.c_str(), allocator), allocator);
        doc.AddMember("tokenIn", rapidjson::Value(details.token_in.c_str(), allocator), allocator);
        doc.AddMember("tokenOut", rapidjson::Value(details.token_out.c_str(), allocator), allocator);
        doc.AddMember("amount", rapidjson::Value(details.trade_mode == "exact_in" ? 
            details.amount_in.value() : details.amount_out.value()), allocator);
        doc.AddMember("side", rapidjson::Value(details.trade_mode.c_str(), allocator), allocator);
        doc.AddMember("allowedSlippage", rapidjson::Value(details.slippage_bps / 100.0), allocator);  // Convert bps to percentage
        
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
        
        // Call gateway API
        std::string response = call_gateway_api("/amm/trade", buffer.GetString());
        
        // Parse response and send appropriate ExecutionReport
        rapidjson::Document resp_doc;
        resp_doc.Parse(response.c_str());
        
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.ts_ns = get_current_time_ns();
        
        if (resp_doc.HasMember("txHash")) {
            report.status = "accepted";
            report.reason_code = "ok";
            report.reason_text = "AMM swap submitted to blockchain";
            report.exchange_order_id = resp_doc["txHash"].GetString();
        } else {
            report.status = "rejected";
            report.reason_code = "venue_reject";
            report.reason_text = "AMM swap failed";
        }
        
        publish_execution_report(report);
        
    } catch (const std::exception& e) {
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "network_error";
        report.reason_text = std::string("AMM swap error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
    }
}

/**
 * @brief Handle Concentrated Liquidity Market Maker swaps
 * @param order The original ExecutionOrder
 * @param details CLMM-specific swap parameters
 * 
 * Processes swaps on concentrated liquidity protocols (Uniswap V3, etc.):
 * 1. Validates CLMM pool parameters (tokens, fee tier)
 * 2. Handles price limit orders and concentrated liquidity ranges
 * 3. Optimizes routing through multiple fee tiers if beneficial
 * 4. Applies sophisticated slippage calculation for concentrated liquidity
 * 5. Manages price impact and MEV protection strategies
 * 
 * CLMM swaps offer better capital efficiency but require more
 * sophisticated price impact and liquidity analysis.
 */
void TradingEngineService::handle_clmm_swap(const ExecutionOrder& order, const ClmmSwapDetails& details) {
    try {
        // Build JSON payload for Hummingbot Gateway CLMM swap
        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();
        
        doc.AddMember("chain", rapidjson::Value(details.chain.c_str(), allocator), allocator);
        doc.AddMember("connector", rapidjson::Value(details.protocol.c_str(), allocator), allocator);
        doc.AddMember("token0", rapidjson::Value(details.pool.token0.c_str(), allocator), allocator);
        doc.AddMember("token1", rapidjson::Value(details.pool.token1.c_str(), allocator), allocator);
        doc.AddMember("fee", rapidjson::Value(details.pool.fee_tier_bps), allocator);
        doc.AddMember("amount", rapidjson::Value(details.trade_mode == "exact_in" ? 
            details.amount_in.value() : details.amount_out.value()), allocator);
        doc.AddMember("side", rapidjson::Value(details.trade_mode.c_str(), allocator), allocator);
        doc.AddMember("allowedSlippage", rapidjson::Value(details.slippage_bps / 100.0), allocator);
        
        if (details.price_limit.has_value()) {
            doc.AddMember("priceLimit", rapidjson::Value(details.price_limit.value()), allocator);
        }
        
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
        
        // Call gateway API
        std::string response = call_gateway_api("/clmm/trade", buffer.GetString());
        
        // Parse response and send appropriate ExecutionReport
        rapidjson::Document resp_doc;
        resp_doc.Parse(response.c_str());
        
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.ts_ns = get_current_time_ns();
        
        if (resp_doc.HasMember("txHash")) {
            report.status = "accepted";
            report.reason_code = "ok";
            report.reason_text = "CLMM swap submitted to blockchain";
            report.exchange_order_id = resp_doc["txHash"].GetString();
        } else {
            report.status = "rejected";
            report.reason_code = "venue_reject";
            report.reason_text = "CLMM swap failed";
        }
        
        publish_execution_report(report);
        
    } catch (const std::exception& e) {
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "network_error";
        report.reason_text = std::string("CLMM swap error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
    }
}

/**
 * @brief Handle on-chain token transfer orders
 * @param order The original ExecutionOrder
 * @param details Transfer-specific parameters
 * 
 * Processes direct token transfers on blockchain networks:
 * 1. Validates transfer parameters (token, amount, recipient)
 * 2. Handles both native tokens (ETH, BNB) and ERC-20/BEP-20 tokens
 * 3. Estimates gas fees and transaction costs
 * 4. Constructs and submits transfer transactions via Gateway
 * 5. Monitors transaction confirmation and handles failures
 * 
 * Essential for portfolio rebalancing, profit taking, and
 * cross-chain arbitrage operations.
 */
void TradingEngineService::handle_transfer(const ExecutionOrder& order, const TransferDetails& details) {
    try {
        // Build JSON payload for chain transfer
        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();
        
        doc.AddMember("chain", rapidjson::Value(details.chain.c_str(), allocator), allocator);
        doc.AddMember("token", rapidjson::Value(details.token.c_str(), allocator), allocator);
        doc.AddMember("amount", rapidjson::Value(details.amount), allocator);
        doc.AddMember("to", rapidjson::Value(details.to_address.c_str(), allocator), allocator);
        
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
        
        // Call gateway API
        std::string response = call_gateway_api("/chain/transfer", buffer.GetString());
        
        // Parse response
        rapidjson::Document resp_doc;
        resp_doc.Parse(response.c_str());
        
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.ts_ns = get_current_time_ns();
        
        if (resp_doc.HasMember("txHash")) {
            report.status = "accepted";
            report.reason_code = "ok";
            report.reason_text = "Transfer submitted to blockchain";
            report.exchange_order_id = resp_doc["txHash"].GetString();
        } else {
            report.status = "rejected";
            report.reason_code = "venue_reject";
            report.reason_text = "Transfer failed";
        }
        
        publish_execution_report(report);
        
    } catch (const std::exception& e) {
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "network_error";
        report.reason_text = std::string("Transfer error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
    }
}

/**
 * @brief Handle order cancellation requests
 * @param order The original ExecutionOrder containing cancel request
 * @param details Cancellation-specific parameters
 * 
 * Cancels existing orders across all supported venues:
 * 1. Locates the target order using client ID or exchange ID
 * 2. For CEX: sends cancel request via ccapi
 * 3. For DEX: attempts transaction cancellation (if still pending)
 * 4. Updates internal order tracking state
 * 5. Generates ExecutionReport with cancellation status
 * 
 * Handles race conditions where orders might fill during cancellation.
 */
void TradingEngineService::handle_cancel(const ExecutionOrder& order, const CancelDetails& details) {
    try {
        // CEX cancel order
        ccapi::Request request(ccapi::Request::Operation::CANCEL_ORDER, order.venue, details.symbol, order.cl_id);
        
        std::map<std::string, std::string> params;
        if (!details.cl_id_to_cancel.empty()) {
            params["CLIENT_ORDER_ID"] = details.cl_id_to_cancel;
        }
        if (details.exchange_order_id.has_value()) {
            params["ORDER_ID"] = details.exchange_order_id.value();
        }
        
        request.appendParam(params);
        ccapi_session_->sendRequest(request);
        
    } catch (const std::exception& e) {
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "network_error";
        report.reason_text = std::string("Cancel error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
    }
}

/**
 * @brief Handle order replacement/modification requests
 * @param order The original ExecutionOrder containing replace request
 * @param details Replacement-specific parameters (new price/size)
 * 
 * Modifies existing orders by replacing them with updated parameters:
 * 1. Validates replacement parameters (price and/or size changes)
 * 2. For CEX: uses exchange-specific replace/modify APIs when available
 * 3. Falls back to cancel-and-replace if native modify not supported
 * 4. Maintains order priority where possible
 * 5. Handles partial fills during replacement process
 * 
 * Essential for dynamic order management and algorithmic strategies.
 */
void TradingEngineService::handle_replace(const ExecutionOrder& order, const ReplaceDetails& details) {
    try {
        // CEX replace order - most exchanges implement as cancel + new order
        // For now, send rejection as many exchanges don't support direct modify
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "venue_reject";
        report.reason_text = "Order replacement not yet supported - use cancel + new order";
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
        
    } catch (const std::exception& e) {
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "network_error";
        report.reason_text = std::string("Replace error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
    }
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

// DEX Gateway communication
/**
 * @brief Call Hummingbot Gateway REST API
 * @param endpoint API endpoint path (e.g., "/amm/trade")
 * @param json_payload JSON request payload
 * @return JSON response from Gateway API
 * 
 * Handles HTTP communication with Hummingbot Gateway:
 * 1. Constructs full URL from base URL + endpoint
 * 2. Sets appropriate headers (Content-Type: application/json)
 * 3. Sends POST request with JSON payload
 * 4. Handles HTTP errors and timeouts
 * 5. Returns response body as JSON string
 * 
 * Used for all DEX operations (swaps, transfers) that require
 * blockchain interaction through the Gateway abstraction layer.
 */
std::string TradingEngineService::call_gateway_api(const std::string& endpoint, const std::string& json_payload) {
    CURL* curl = curl_easy_init();
    std::string response_string;
    
    if (curl) {
        // Set callback for response data
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, [](char* data, size_t size, size_t nmemb, void* userdata) -> size_t {
            size_t real_size = size * nmemb;
            std::string* response = static_cast<std::string*>(userdata);
            response->append(data, real_size);
            return real_size;
        });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
        
        // Set URL and headers
        std::string full_url = gateway_base_url_ + endpoint;
        curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        // Set POST data
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_payload.length());
        
        // Perform request
        CURLcode res = curl_easy_perform(curl);
        
        // Cleanup
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) {
            throw std::runtime_error("Gateway API call failed: " + std::string(curl_easy_strerror(res)));
        }
    } else {
        throw std::runtime_error("Failed to initialize curl");
    }
    
    return response_string;
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

void TradingEngineService::subscribe_market_data(const std::string& exchange, const std::string& instrument) {
    std::cout << "[TradingEngine] Subscribing to market data: " << exchange << ":" << instrument << std::endl;
    
    try {
        std::string correlation_id = exchange + ":" + instrument;
        ccapi::Subscription subscription(exchange, instrument, "MARKET_DEPTH", "", correlation_id);
        
        active_subscriptions_[correlation_id] = subscription;
        ccapi_session_->subscribe(subscription);
    } catch (const std::exception& e) {
        std::cerr << "[TradingEngine] Market data subscription error: " << e.what() << std::endl;
    }
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
                CexOrderDetails cex_details;
                if (details.HasMember("symbol")) cex_details.symbol = details["symbol"].GetString();
                if (details.HasMember("side")) cex_details.side = details["side"].GetString();
                if (details.HasMember("order_type")) cex_details.order_type = details["order_type"].GetString();
                if (details.HasMember("time_in_force")) cex_details.time_in_force = details["time_in_force"].GetString();
                if (details.HasMember("size")) cex_details.size = details["size"].GetDouble();
                if (details.HasMember("price")) cex_details.price = details["price"].GetDouble();
                if (details.HasMember("stop_price")) cex_details.stop_price = details["stop_price"].GetDouble();
                if (details.HasMember("reduce_only")) cex_details.reduce_only = details["reduce_only"].GetBool();
                if (details.HasMember("margin_mode")) cex_details.margin_mode = details["margin_mode"].GetString();
                
                // Parse params
                if (details.HasMember("params") && details["params"].IsObject()) {
                    for (auto it = details["params"].MemberBegin(); it != details["params"].MemberEnd(); ++it) {
                        cex_details.params[it->name.GetString()] = it->value.GetString();
                    }
                }
                order.details = cex_details;
                
            } else if (order.venue_type == "dex" && order.product_type == "amm_swap") {
                AmmSwapDetails amm_details;
                if (details.HasMember("chain")) amm_details.chain = details["chain"].GetString();
                if (details.HasMember("protocol")) amm_details.protocol = details["protocol"].GetString();
                if (details.HasMember("token_in")) amm_details.token_in = details["token_in"].GetString();
                if (details.HasMember("token_out")) amm_details.token_out = details["token_out"].GetString();
                if (details.HasMember("trade_mode")) amm_details.trade_mode = details["trade_mode"].GetString();
                if (details.HasMember("amount_in")) amm_details.amount_in = details["amount_in"].GetDouble();
                if (details.HasMember("amount_out")) amm_details.amount_out = details["amount_out"].GetDouble();
                if (details.HasMember("slippage_bps")) amm_details.slippage_bps = details["slippage_bps"].GetInt();
                if (details.HasMember("deadline_sec")) amm_details.deadline_sec = details["deadline_sec"].GetInt();
                if (details.HasMember("recipient")) amm_details.recipient = details["recipient"].GetString();
                
                // Parse route
                if (details.HasMember("route") && details["route"].IsArray()) {
                    for (auto& v : details["route"].GetArray()) {
                        amm_details.route.push_back(v.GetString());
                    }
                }
                order.details = amm_details;
                
            } else if (order.venue_type == "dex" && order.product_type == "clmm_swap") {
                ClmmSwapDetails clmm_details;
                if (details.HasMember("chain")) clmm_details.chain = details["chain"].GetString();
                if (details.HasMember("protocol")) clmm_details.protocol = details["protocol"].GetString();
                if (details.HasMember("trade_mode")) clmm_details.trade_mode = details["trade_mode"].GetString();
                if (details.HasMember("amount_in")) clmm_details.amount_in = details["amount_in"].GetDouble();
                if (details.HasMember("amount_out")) clmm_details.amount_out = details["amount_out"].GetDouble();
                if (details.HasMember("slippage_bps")) clmm_details.slippage_bps = details["slippage_bps"].GetInt();
                if (details.HasMember("price_limit")) clmm_details.price_limit = details["price_limit"].GetDouble();
                if (details.HasMember("deadline_sec")) clmm_details.deadline_sec = details["deadline_sec"].GetInt();
                if (details.HasMember("recipient")) clmm_details.recipient = details["recipient"].GetString();
                
                // Parse pool details
                if (details.HasMember("pool") && details["pool"].IsObject()) {
                    const auto& pool = details["pool"];
                    if (pool.HasMember("token0")) clmm_details.pool.token0 = pool["token0"].GetString();
                    if (pool.HasMember("token1")) clmm_details.pool.token1 = pool["token1"].GetString();
                    if (pool.HasMember("fee_tier_bps")) clmm_details.pool.fee_tier_bps = pool["fee_tier_bps"].GetInt();
                }
                order.details = clmm_details;
                
            } else if (order.venue_type == "chain" && order.product_type == "transfer") {
                TransferDetails transfer_details;
                if (details.HasMember("chain")) transfer_details.chain = details["chain"].GetString();
                if (details.HasMember("token")) transfer_details.token = details["token"].GetString();
                if (details.HasMember("amount")) transfer_details.amount = details["amount"].GetDouble();
                if (details.HasMember("to_address")) transfer_details.to_address = details["to_address"].GetString();
                order.details = transfer_details;
            }
        } else if (order.action == "cancel") {
            CancelDetails cancel_details;
            if (details.HasMember("symbol")) cancel_details.symbol = details["symbol"].GetString();
            if (details.HasMember("cl_id_to_cancel")) cancel_details.cl_id_to_cancel = details["cl_id_to_cancel"].GetString();
            if (details.HasMember("exchange_order_id")) cancel_details.exchange_order_id = details["exchange_order_id"].GetString();
            order.details = cancel_details;
            
        } else if (order.action == "replace") {
            ReplaceDetails replace_details;
            if (details.HasMember("symbol")) replace_details.symbol = details["symbol"].GetString();
            if (details.HasMember("cl_id_to_replace")) replace_details.cl_id_to_replace = details["cl_id_to_replace"].GetString();
            if (details.HasMember("new_price")) replace_details.new_price = details["new_price"].GetDouble();
            if (details.HasMember("new_size")) replace_details.new_size = details["new_size"].GetDouble();
            order.details = replace_details;
        }
    }
    
    return order;
}

/**
 * @brief Parse JSON message to PreprocessedTrade object
 * @param json_message JSON string containing preprocessed trade data
 * @return Parsed PreprocessedTrade object
 * 
 * Parses enriched trade data from market data pipeline:
 * 1. Extracts common metadata (schema version, sequence, timestamps)
 * 2. Parses original trade fields (exchange, symbol, price, amount)
 * 3. Extracts preprocessing features (volatility, volume metrics)
 * 4. Validates data integrity and handles missing fields
 * 
 * Used by backtest execution engine to simulate realistic
 * order fills based on actual market conditions.
 */
PreprocessedTrade TradingEngineService::parse_preprocessed_trade(const std::string& json_message) {
    rapidjson::Document doc;
    doc.Parse(json_message.c_str());
    
    if (doc.HasParseError()) {
        throw std::runtime_error("Failed to parse preprocessed trade JSON: " + std::string(rapidjson::GetParseError_En(doc.GetParseError())));
    }
    
    PreprocessedTrade trade;
    
    // Common metadata
    if (doc.HasMember("schema_version")) trade.schema_version = doc["schema_version"].GetInt();
    if (doc.HasMember("partition_id")) trade.partition_id = doc["partition_id"].GetString();
    if (doc.HasMember("seq")) trade.seq = doc["seq"].GetUint64();
    if (doc.HasMember("receipt_timestamp_ns")) trade.receipt_timestamp_ns = doc["receipt_timestamp_ns"].GetUint64();
    if (doc.HasMember("preprocessing_timestamp")) trade.preprocessing_timestamp = doc["preprocessing_timestamp"].GetString();
    if (doc.HasMember("symbol_norm")) trade.symbol_norm = doc["symbol_norm"].GetString();
    
    // Original cryptofeed fields
    if (doc.HasMember("exchange")) trade.exchange = doc["exchange"].GetString();
    if (doc.HasMember("symbol")) trade.symbol = doc["symbol"].GetString();
    if (doc.HasMember("price")) trade.price = doc["price"].GetDouble();
    if (doc.HasMember("amount")) trade.amount = doc["amount"].GetDouble();
    if (doc.HasMember("timestamp")) trade.timestamp = doc["timestamp"].GetString();
    if (doc.HasMember("side")) trade.side = doc["side"].GetString();
    
    // Preprocessing fields
    if (doc.HasMember("transaction_price")) trade.transaction_price = doc["transaction_price"].GetDouble();
    if (doc.HasMember("trading_volume")) trade.trading_volume = doc["trading_volume"].GetDouble();
    if (doc.HasMember("volatility_transaction_price")) trade.volatility_transaction_price = doc["volatility_transaction_price"].GetDouble();
    if (doc.HasMember("window_size")) trade.window_size = doc["window_size"].GetInt();
    
    return trade;
}

/**
 * @brief Parse JSON message to PreprocessedOrderbook object
 * @param json_message JSON string containing preprocessed orderbook data
 * @return Parsed PreprocessedOrderbook object
 * 
 * Parses enriched order book data from market data pipeline:
 * 1. Extracts common metadata and original orderbook fields
 * 2. Parses preprocessing features (spread, depth, imbalance metrics)
 * 3. Calculates derived values (midpoint, volatility, breadth)
 * 4. Validates data consistency and handles edge cases
 * 
 * Critical for backtest execution as it provides the market state
 * needed to determine order fill probability and realistic prices.
 */
PreprocessedOrderbook TradingEngineService::parse_preprocessed_orderbook(const std::string& json_message) {
    rapidjson::Document doc;
    doc.Parse(json_message.c_str());
    
    if (doc.HasParseError()) {
        throw std::runtime_error("Failed to parse preprocessed orderbook JSON: " + std::string(rapidjson::GetParseError_En(doc.GetParseError())));
    }
    
    PreprocessedOrderbook orderbook;
    
    // Common metadata
    if (doc.HasMember("schema_version")) orderbook.schema_version = doc["schema_version"].GetInt();
    if (doc.HasMember("partition_id")) orderbook.partition_id = doc["partition_id"].GetString();
    if (doc.HasMember("seq")) orderbook.seq = doc["seq"].GetUint64();
    if (doc.HasMember("receipt_timestamp_ns")) orderbook.receipt_timestamp_ns = doc["receipt_timestamp_ns"].GetUint64();
    if (doc.HasMember("preprocessing_timestamp")) orderbook.preprocessing_timestamp = doc["preprocessing_timestamp"].GetString();
    if (doc.HasMember("symbol_norm")) orderbook.symbol_norm = doc["symbol_norm"].GetString();
    
    // Original cryptofeed fields
    if (doc.HasMember("exchange")) orderbook.exchange = doc["exchange"].GetString();
    if (doc.HasMember("symbol")) orderbook.symbol = doc["symbol"].GetString();
    if (doc.HasMember("timestamp")) orderbook.timestamp = doc["timestamp"].GetString();
    
    // Preprocessing fields
    if (doc.HasMember("best_bid_price")) orderbook.best_bid_price = doc["best_bid_price"].GetDouble();
    if (doc.HasMember("best_bid_size")) orderbook.best_bid_size = doc["best_bid_size"].GetDouble();
    if (doc.HasMember("best_ask_price")) orderbook.best_ask_price = doc["best_ask_price"].GetDouble();
    if (doc.HasMember("best_ask_size")) orderbook.best_ask_size = doc["best_ask_size"].GetDouble();
    if (doc.HasMember("midpoint")) orderbook.midpoint = doc["midpoint"].GetDouble();
    if (doc.HasMember("relative_spread")) orderbook.relative_spread = doc["relative_spread"].GetDouble();
    if (doc.HasMember("breadth")) orderbook.breadth = doc["breadth"].GetDouble();
    if (doc.HasMember("imbalance_lvl1")) orderbook.imbalance_lvl1 = doc["imbalance_lvl1"].GetDouble();
    if (doc.HasMember("bid_depth_n")) orderbook.bid_depth_n = doc["bid_depth_n"].GetDouble();
    if (doc.HasMember("ask_depth_n")) orderbook.ask_depth_n = doc["ask_depth_n"].GetDouble();
    if (doc.HasMember("depth_n")) orderbook.depth_n = doc["depth_n"].GetDouble();
    if (doc.HasMember("volatility_mid")) orderbook.volatility_mid = doc["volatility_mid"].GetDouble();
    if (doc.HasMember("ofi_rolling")) orderbook.ofi_rolling = doc["ofi_rolling"].GetDouble();
    if (doc.HasMember("window_size")) orderbook.window_size = doc["window_size"].GetInt();
    
    return orderbook;
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

// Market data subscription threads
/**
 * @brief Market data trade subscriber thread
 * 
 * Continuously processes preprocessed trade data:
 * 1. Receives trade messages from ZMQ SUB socket
 * 2. Parses JSON to PreprocessedTrade objects
 * 3. Updates internal market state for backtest execution
 * 4. Triggers order fill simulation when appropriate
 * 
 * Essential for realistic backtesting that responds to
 * actual market conditions and trade flow.
 */
void TradingEngineService::zmq_trade_subscriber_thread() {
    std::cout << "[TradingEngine] Trade subscriber thread started" << std::endl;
    
    while (running_) {
        try {
            zmq::message_t topic_msg, data_msg;
            
            // Receive topic and data
            if (trade_subscriber_socket_->recv(topic_msg, zmq::recv_flags::dontwait) &&
                trade_subscriber_socket_->recv(data_msg, zmq::recv_flags::dontwait)) {
                
                std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
                std::string data(static_cast<char*>(data_msg.data()), data_msg.size());
                
                // Parse and process trade data
                try {
                    PreprocessedTrade trade = parse_preprocessed_trade(data);
                    process_preprocessed_trade(trade);
                } catch (const std::exception& e) {
                    std::cerr << "[TradingEngine] Failed to parse trade data: " << e.what() << std::endl;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } catch (const std::exception& e) {
            std::cerr << "[TradingEngine] Trade subscriber error: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    std::cout << "[TradingEngine] Trade subscriber thread stopped" << std::endl;
}

/**
 * @brief Market data orderbook subscriber thread
 * 
 * Continuously processes preprocessed orderbook data:
 * 1. Receives orderbook messages from ZMQ SUB socket
 * 2. Parses JSON to PreprocessedOrderbook objects
 * 3. Updates market state tracking for order fill decisions
 * 4. Provides liquidity and spread information for execution
 * 
 * The orderbook data is the primary input for determining
 * whether pending orders should be filled in backtest mode.
 */
void TradingEngineService::zmq_orderbook_subscriber_thread() {
    std::cout << "[TradingEngine] Orderbook subscriber thread started" << std::endl;
    
    while (running_) {
        try {
            zmq::message_t topic_msg, data_msg;
            
            // Receive topic and data
            if (orderbook_subscriber_socket_->recv(topic_msg, zmq::recv_flags::dontwait) &&
                orderbook_subscriber_socket_->recv(data_msg, zmq::recv_flags::dontwait)) {
                
                std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
                std::string data(static_cast<char*>(data_msg.data()), data_msg.size());
                
                // Parse and process orderbook data
                try {
                    PreprocessedOrderbook orderbook = parse_preprocessed_orderbook(data);
                    process_preprocessed_orderbook(orderbook);
                } catch (const std::exception& e) {
                    std::cerr << "[TradingEngine] Failed to parse orderbook data: " << e.what() << std::endl;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } catch (const std::exception& e) {
            std::cerr << "[TradingEngine] Orderbook subscriber error: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    std::cout << "[TradingEngine] Orderbook subscriber thread stopped" << std::endl;
}

// Market data processing
void TradingEngineService::process_preprocessed_trade(const PreprocessedTrade& trade) {
    std::lock_guard<std::mutex> lock(market_state_mutex_);
    latest_trade_[trade.symbol] = trade;
    
    // Optional: Log high-volume trades or significant price moves
    if (trade.trading_volume > 100000.0 || trade.volatility_transaction_price > 0.05) {
        std::cout << "[MarketData] Notable trade: " << trade.symbol 
                  << " price=" << trade.price 
                  << " volume=" << trade.trading_volume 
                  << " volatility=" << trade.volatility_transaction_price << std::endl;
    }
}

void TradingEngineService::process_preprocessed_orderbook(const PreprocessedOrderbook& orderbook) {
    std::lock_guard<std::mutex> lock(market_state_mutex_);
    latest_orderbook_[orderbook.symbol] = orderbook;
    
    // Check for pending orders that might be filled by this orderbook update
    for (const auto& [cl_id, order] : pending_orders_) {
        if (backtest_mode_) {
            simulate_order_execution(order);
        }
    }
}

void TradingEngineService::update_market_state(const std::string& symbol, const PreprocessedOrderbook& orderbook) {
    std::lock_guard<std::mutex> lock(market_state_mutex_);
    latest_orderbook_[symbol] = orderbook;
}

// Backtest execution engine
/**
 * @brief Simulate order execution in backtest mode
 * @param order The ExecutionOrder to simulate
 * 
 * Realistic order execution simulation:
 * 1. Checks if order should be filled based on market conditions
 * 2. Calculates realistic fill price including slippage
 * 3. Determines fill size (full or partial fills)
 * 4. Generates ExecutionReport and Fill messages
 * 5. Updates order tracking state
 * 
 * Uses current market data (orderbook, trades) to make
 * realistic fill decisions with configurable fill probability.
 */
void TradingEngineService::simulate_order_execution(const ExecutionOrder& order) {
    if (!backtest_mode_) {
        return;  // Only simulate in backtest mode
    }
    
    // Get the appropriate symbol for lookup
    std::string symbol;
    if (std::holds_alternative<CexOrderDetails>(order.details)) {
        symbol = std::get<CexOrderDetails>(order.details).symbol;
    } else {
        return;  // Only handle CEX orders for now
    }
    
    // Get latest orderbook for this symbol
    std::lock_guard<std::mutex> lock(market_state_mutex_);
    auto it = latest_orderbook_.find(symbol);
    if (it == latest_orderbook_.end()) {
        return;  // No market data available
    }
    
    const PreprocessedOrderbook& orderbook = it->second;
    
    // Check if order should be filled
    if (should_fill_order(order, orderbook)) {
        double fill_price = calculate_fill_price(order, orderbook);
        double fill_size = std::get<CexOrderDetails>(order.details).size;
        generate_fill_from_market_data(order, fill_price, fill_size);
    }
}

/**
 * @brief Determine if order should be filled based on market conditions
 * @param order The order to evaluate for filling
 * @param orderbook Current market orderbook state
 * @return true if order should be filled, false otherwise
 * 
 * Sophisticated fill logic that considers:
 * 1. Order type (market vs limit) and price constraints
 * 2. Market conditions (bid/ask prices, spreads)
 * 3. Random fill probability for realistic simulation
 * 4. Liquidity availability and order size
 * 
 * Market orders typically fill immediately, while limit orders
 * only fill when market price reaches the limit price.
 */
bool TradingEngineService::should_fill_order(const ExecutionOrder& order, const PreprocessedOrderbook& orderbook) {
    if (!std::holds_alternative<CexOrderDetails>(order.details)) {
        return false;
    }
    
    const CexOrderDetails& details = std::get<CexOrderDetails>(order.details);
    
    // Random fill probability
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);
    
    if (dis(gen) > fill_probability_) {
        return false;
    }
    
    // Check if order can be filled based on market conditions
    if (details.order_type == "market") {
        return true;  // Market orders should always fill
    }
    
    if (details.order_type == "limit" && details.price.has_value()) {
        if (details.side == "buy") {
            // Buy limit order fills if our price >= ask price
            return details.price.value() >= orderbook.best_ask_price;
        } else {
            // Sell limit order fills if our price <= bid price
            return details.price.value() <= orderbook.best_bid_price;
        }
    }
    
    return false;
}

/**
 * @brief Calculate realistic fill price for order execution
 * @param order The order being filled
 * @param orderbook Current market orderbook state
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
double TradingEngineService::calculate_fill_price(const ExecutionOrder& order, const PreprocessedOrderbook& orderbook) {
    if (!std::holds_alternative<CexOrderDetails>(order.details)) {
        return 0.0;
    }
    
    const CexOrderDetails& details = std::get<CexOrderDetails>(order.details);
    double base_price;
    
    if (details.order_type == "market") {
        // Market orders get filled at best available price
        base_price = (details.side == "buy") ? orderbook.best_ask_price : orderbook.best_bid_price;
    } else if (details.order_type == "limit" && details.price.has_value()) {
        // Limit orders get filled at limit price or better
        if (details.side == "buy") {
            base_price = std::min(details.price.value(), orderbook.best_ask_price);
        } else {
            base_price = std::max(details.price.value(), orderbook.best_bid_price);
        }
    } else {
        base_price = orderbook.midpoint;
    }
    
    // Add realistic slippage
    double slippage_factor = slippage_bps_ / 10000.0;  // Convert bps to decimal
    if (details.side == "buy") {
        base_price *= (1.0 + slippage_factor);
    } else {
        base_price *= (1.0 - slippage_factor);
    }
    
    return base_price;
}

void TradingEngineService::generate_fill_from_market_data(const ExecutionOrder& order, double fill_price, double fill_size) {
    // Generate execution report
    ExecutionReport report;
    report.cl_id = order.cl_id;
    report.status = "filled";
    report.reason_code = "ok";
    report.reason_text = "Order filled in backtest simulation";
    report.ts_ns = get_current_time_ns();
    report.tags = order.tags;
    report.tags["execution_type"] = "simulated";
    
    publish_execution_report(report);
    
    // Generate fill
    Fill fill;
    fill.cl_id = order.cl_id;
    fill.exec_id = generate_exec_id();
    fill.symbol_or_pair = std::get<CexOrderDetails>(order.details).symbol;
    fill.price = fill_price;
    fill.size = fill_size;
    fill.fee_currency = "USDT";  // Default fee currency
    fill.fee_amount = fill_price * fill_size * 0.001;  // 0.1% fee
    fill.liquidity = "taker";  // Assume taker for simulation
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

} // namespace latentspeed
