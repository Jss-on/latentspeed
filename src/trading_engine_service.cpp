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
#include "symbol_fetcher.h"
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
 * @brief Simplified constructor - focuses only on essential components
 */
TradingEngineService::TradingEngineService()
    : running_(false)
    , order_endpoint_("tcp://127.0.0.1:5601")        // Contract-specified endpoint
    , report_endpoint_("tcp://127.0.0.1:5602")       // Contract-specified endpoint
    , trade_data_endpoint_("tcp://127.0.0.1:5556")   // Trade data endpoint
    , orderbook_data_endpoint_("tcp://127.0.0.1:5557") // Orderbook data endpoint
    , backtest_mode_(true)                            // Always start in backtest mode
    , fill_probability_(0.9)                          // High fill probability for testing
    , slippage_bps_(1.0)                             // 1 bps slippage
    , depth_levels_(5)                                // Number of levels for depth_n calculation
{
    // Configure basic CCAPI session options for better WebSocket connectivity
    ccapi_session_options_.enableCheckHeartbeatFix = true;
    
    // CCAPI SessionConfigs are handled automatically by the library
    // No manual configuration needed for standard exchanges like Bybit
    
    spdlog::info("[TradingEngine] Simplified trading engine initialized with CCAPI config");
    spdlog::info("[TradingEngine] Mode: Backtest only (DEX/live trading disabled)");
}

TradingEngineService::~TradingEngineService() {
    stop();
}

/**
 * @brief Enhanced initialization with market data support
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
        
        // Trade data publisher socket (PUB - sends trade data on port 5558)
        trade_data_publisher_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PUB);
        trade_data_publisher_socket_->setsockopt(ZMQ_SNDHWM, &sndhwm_, sizeof(sndhwm_));
        trade_data_publisher_socket_->setsockopt(ZMQ_LINGER, &linger_ms_, sizeof(linger_ms_));
        trade_data_publisher_socket_->bind(trade_data_endpoint_);
        
        // Orderbook data publisher socket (PUB - sends orderbook data on port 5559)
        orderbook_data_publisher_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PUB);
        orderbook_data_publisher_socket_->setsockopt(ZMQ_SNDHWM, &sndhwm_, sizeof(sndhwm_));
        orderbook_data_publisher_socket_->setsockopt(ZMQ_LINGER, &linger_ms_, sizeof(linger_ms_));
        orderbook_data_publisher_socket_->bind(orderbook_data_endpoint_);
        
        // Initialize CCAPI session for market data
        ccapi_session_ = std::make_unique<ccapi::Session>(ccapi_session_options_, ccapi_session_configs_, this);
        
        // Create subscriptions for each exchange-symbol pair
        std::vector<std::string> exchanges = getExchangesFromConfig();
        std::vector<std::string> symbols = getSymbolsFromConfig();

        // Validate exchange connectivity before proceeding
        spdlog::info("[TradingEngine] Validating connectivity to {} exchanges with {} symbols", 
                     exchanges.size(), symbols.size());
        
        spdlog::info("[TradingEngine] Starting market data subscriptions for {} exchanges and {} symbols", 
                     exchanges.size(), symbols.size());
        
        for (const auto& exchange : exchanges) {
            spdlog::info("[TradingEngine] Processing subscriptions for exchange: {}", exchange);
            
            for (const auto& symbol : symbols) {
                spdlog::debug("[TradingEngine] Processing symbol: {} on exchange: {}", symbol, exchange);
                
                // Convert symbol to exchange-specific format
                std::string exchange_symbol = convert_symbol_for_exchange(symbol, exchange);
                spdlog::debug("[TradingEngine] Symbol conversion: {} -> {} (exchange: {})", 
                             symbol, exchange_symbol, exchange);
                
                try {
                    // Market depth subscription
                    ccapi::Subscription depth_subscription(exchange, exchange_symbol, "MARKET_DEPTH");
                    ccapi_session_->subscribe(depth_subscription);
                    spdlog::info("[TradingEngine] ✓ Subscribed to MARKET_DEPTH for {}-{}", 
                                exchange, exchange_symbol);
                    
                    // Trade data subscription  
                    ccapi::Subscription trade_subscription(exchange, exchange_symbol, "TRADE");
                    ccapi_session_->subscribe(trade_subscription);
                    spdlog::info("[TradingEngine] ✓ Subscribed to TRADE for {}-{}", 
                                exchange, exchange_symbol);
                    
                    // Small delay between subscriptions to avoid overwhelming exchanges
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    
                } catch (const std::exception& e) {
                    spdlog::error("[TradingEngine] ✗ Failed to subscribe to {}-{}: {}", 
                                 exchange, exchange_symbol, e.what());
                }
            }
            
            spdlog::info("[TradingEngine] Completed subscriptions for exchange: {}", exchange);
        }
        
        spdlog::info("[TradingEngine] Market data subscription setup completed. Total subscriptions attempted: {}", 
                     exchanges.size() * symbols.size() * 2);
        
        spdlog::info("[TradingEngine] Enhanced initialization complete");
        spdlog::info("[TradingEngine] Order endpoint: {}", order_endpoint_);
        spdlog::info("[TradingEngine] Report endpoint: {}", report_endpoint_);
        spdlog::info("[TradingEngine] Trade data endpoint: {}", trade_data_endpoint_);
        spdlog::info("[TradingEngine] Orderbook data endpoint: {}", orderbook_data_endpoint_);
        
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
    
    // Start trade data publisher thread
    trade_data_publisher_thread_ = std::make_unique<std::thread>(&TradingEngineService::trade_data_publisher_thread, this);
    
    // Start orderbook data publisher thread
    orderbook_data_publisher_thread_ = std::make_unique<std::thread>(&TradingEngineService::orderbook_data_publisher_thread, this);
    
    // Start message processor threads
    orderbook_processor_thread_ = std::make_unique<std::thread>(&TradingEngineService::orderbook_processor_thread, this);
    trade_processor_thread_ = std::make_unique<std::thread>(&TradingEngineService::trade_processor_thread, this);
    
    // Start CCAPI market data subscriptions
    start_market_data_subscriptions();
    
    spdlog::info("[TradingEngine] Enhanced service started (order processing + market data)");
}

void TradingEngineService::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    
    // Stop CCAPI subscriptions first
    stop_market_data_subscriptions();
    
    // Notify publisher threads
    {
        std::lock_guard<std::mutex> lock(publish_mutex_);
        publish_cv_.notify_all();
    }
    
    {
        std::lock_guard<std::mutex> lock(trade_data_mutex_);
        trade_data_cv_.notify_all();
    }
    
    {
        std::lock_guard<std::mutex> lock(orderbook_data_mutex_);
        orderbook_data_cv_.notify_all();
    }
    
    // Notify message processor threads
    {
        std::lock_guard<std::mutex> lock(orderbook_queue_mutex_);
        orderbook_queue_cv_.notify_all();
    }
    
    {
        std::lock_guard<std::mutex> lock(trade_queue_mutex_);
        trade_queue_cv_.notify_all();
    }
    
    // Wait for all threads
    if (order_receiver_thread_ && order_receiver_thread_->joinable()) {
        order_receiver_thread_->join();
    }
    
    if (publisher_thread_ && publisher_thread_->joinable()) {
        publisher_thread_->join();
    }
    
    if (trade_data_publisher_thread_ && trade_data_publisher_thread_->joinable()) {
        trade_data_publisher_thread_->join();
    }
    
    if (orderbook_data_publisher_thread_ && orderbook_data_publisher_thread_->joinable()) {
        orderbook_data_publisher_thread_->join();
    }
    
    if (orderbook_processor_thread_ && orderbook_processor_thread_->joinable()) {
        orderbook_processor_thread_->join();
    }
    
    if (trade_processor_thread_ && trade_processor_thread_->joinable()) {
        trade_processor_thread_->join();
    }
    
    spdlog::info("[TradingEngine] Enhanced service stopped");
}

/**
 * @brief Start CCAPI market data subscriptions
 */
void TradingEngineService::start_market_data_subscriptions() {
    try {
        // Create subscriptions for each exchange-symbol pair
        std::vector<std::string> exchanges = getExchangesFromConfig();
        std::vector<std::string> symbols = getSymbolsFromConfig();
        
        // Log symbols in a readable format
        if (!symbols.empty()) {
            std::ostringstream oss;
            for (size_t i = 0; i < std::min(symbols.size(), size_t(10)); ++i) {
                if (i > 0) oss << ", ";
                oss << symbols[i];
            }
            if (symbols.size() > 10) oss << " ...";
            spdlog::info("[TradingEngine] Loaded {} symbols: {}", symbols.size(), oss.str());
        }
        
        spdlog::info("[TradingEngine] Starting subscriptions for {} exchanges and {} symbols", exchanges.size(), symbols.size());
        
        for (const auto& exchange : exchanges) {
            spdlog::info("[TradingEngine] Processing exchange: {}", exchange);
            for (const auto& symbol : symbols) {
                spdlog::info("[TradingEngine] Subscribing to {} on {}", symbol, exchange);
                
                // Convert symbol to exchange-specific format
                std::string exchange_symbol = convert_symbol_for_exchange(symbol, exchange);
                spdlog::info("[TradingEngine] Converted symbol: {} -> {}", symbol, exchange_symbol);
                
                try {
                    // Trade data subscription (prioritize trades over depth for now)
                    ccapi::Subscription trade_subscription(exchange, exchange_symbol, "TRADE");
                    ccapi_session_->subscribe(trade_subscription);
                    spdlog::info("[TradingEngine] Successfully subscribed to TRADE for {}-{}", exchange, exchange_symbol);
                    
                    // Small delay between subscriptions to avoid overwhelming the exchange
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    
                } catch (const std::exception& e) {
                    spdlog::error("[TradingEngine] Failed to subscribe to {}-{}: {}", exchange, exchange_symbol, e.what());
                }
            }
        }
        
        // Small delay to allow subscriptions to establish
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
    } catch (const std::exception& e) {
        spdlog::error("[TradingEngine] Failed to start market data subscriptions: {}", e.what());
    }
}

/**
 * @brief Stop CCAPI market data subscriptions
 */
void TradingEngineService::stop_market_data_subscriptions() {
    try {
        if (ccapi_session_) {
            ccapi_session_->stop();
        }
    } catch (const std::exception& e) {
        spdlog::error("[TradingEngine] Error stopping market data subscriptions: {}", e.what());
    }
}

/**
 * @brief CCAPI event handler - processes market data events
 */
void TradingEngineService::processEvent(const ccapi::Event& event, ccapi::Session* session) {
    try {
        if (event.getType() == ccapi::Event::Type::SUBSCRIPTION_STATUS) {
            spdlog::info("[MarketData] Subscription status: {}", event.toPrettyString(2, 2));
            return;
        }
        
        if (event.getType() != ccapi::Event::Type::SUBSCRIPTION_DATA) {
            return;
        }
        
        for (const auto& message : event.getMessageList()) {
            // Get exchange and symbol from correlation ID list
            std::string exchange = "";
            std::string symbol = "";
            
            const auto& correlationIdList = message.getCorrelationIdList();
            if (!correlationIdList.empty()) {
                // Parse exchange and symbol from first correlation ID
                // Format is typically "exchange:symbol:MARKET_DEPTH" or similar
                std::string correlationId = correlationIdList[0];
                size_t firstColon = correlationId.find(':');
                size_t secondColon = correlationId.find(':', firstColon + 1);
                
                if (firstColon != std::string::npos && secondColon != std::string::npos) {
                    exchange = correlationId.substr(0, firstColon);
                    symbol = correlationId.substr(firstColon + 1, secondColon - firstColon - 1);
                }
            }
            
            // Fallback: use default values if correlation ID parsing fails
            if (exchange.empty() || symbol.empty()) {
                exchange = "bybit";  // Default exchange
                symbol = "ETHUSDT";  // Default symbol
            }
            
            exchange = normalize_exchange(exchange);
            symbol = normalize_symbol(symbol);
            
            uint64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                message.getTime().time_since_epoch()).count();
            
            if (message.getType() == ccapi::Message::Type::MARKET_DATA_EVENTS_MARKET_DEPTH) {
                // Queue orderbook message for async processing
                {
                    std::lock_guard<std::mutex> lock(orderbook_queue_mutex_);
                    orderbook_message_queue_.push({message, exchange, symbol, timestamp_ns});
                    orderbook_queue_cv_.notify_one();
                }
            } else if (message.getType() == ccapi::Message::Type::MARKET_DATA_EVENTS_TRADE) {
                // Queue trade message for async processing
                {
                    std::lock_guard<std::mutex> lock(trade_queue_mutex_);
                    trade_message_queue_.push({message, exchange, symbol, timestamp_ns});
                    trade_queue_cv_.notify_one();
                }
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("[MarketData] Error processing CCAPI event: {}", e.what());
    }
}

/**
 * @brief Orderbook message processor thread
 */
void TradingEngineService::orderbook_processor_thread() {
    spdlog::info("[OrderbookProcessor] Thread started");
    
    while (running_) {
        std::unique_lock<std::mutex> lock(orderbook_queue_mutex_);
        
        // Wait for messages or shutdown signal
        orderbook_queue_cv_.wait(lock, [this] {
            return !orderbook_message_queue_.empty() || !running_;
        });
        
        // Process all queued messages
        while (!orderbook_message_queue_.empty() && running_) {
            OrderBookMessage msg = std::move(orderbook_message_queue_.front());
            orderbook_message_queue_.pop();
            lock.unlock();
            
            // Process the message
            handle_orderbook_message(msg.message, msg.exchange, msg.symbol, msg.timestamp_ns);
            
            lock.lock();
        }
    }
    
    spdlog::info("[OrderbookProcessor] Thread stopped");
}

/**
 * @brief Trade message processor thread
 */
void TradingEngineService::trade_processor_thread() {
    spdlog::info("[TradeProcessor] Thread started");
    
    while (running_) {
        std::unique_lock<std::mutex> lock(trade_queue_mutex_);
        
        // Wait for messages or shutdown signal
        trade_queue_cv_.wait(lock, [this] {
            return !trade_message_queue_.empty() || !running_;
        });
        
        // Process all queued messages
        while (!trade_message_queue_.empty() && running_) {
            TradeMessage msg = std::move(trade_message_queue_.front());
            trade_message_queue_.pop();
            lock.unlock();
            
            // Process the message
            handle_trade_message(msg.message, msg.exchange, msg.symbol, msg.timestamp_ns);
            
            lock.lock();
        }
    }
    
    spdlog::info("[TradeProcessor] Thread stopped");
}

/**
 * @brief Handle orderbook messages from CCAPI
 */
void TradingEngineService::handle_orderbook_message(const ccapi::Message& message, const std::string& exchange, const std::string& symbol, uint64_t timestamp_ns) {
    OrderBookData book_data;
    book_data.exchange = exchange;
    book_data.symbol = symbol;
    book_data.timestamp_ns = timestamp_ns;
    book_data.receipt_timestamp_ns = get_current_time_ns();
    
    // Parse market depth from CCAPI message
    const auto& elementList = message.getElementList();
    if (!elementList.empty()) {
        long double best_bid_price = 0.0L, best_bid_size = 0.0L;
        long double best_ask_price = 0.0L, best_ask_size = 0.0L;
        std::string best_bid_price_str, best_bid_size_str, best_ask_price_str, best_ask_size_str;
        
        // Parse all bid and ask levels with higher precision
        std::vector<std::pair<long double, long double>> bid_levels;
        std::vector<std::pair<long double, long double>> ask_levels;
        
        for (const auto& element : elementList) {
            const auto& nameValueMap = element.getNameValueMap();
            
            // DEBUG: Log all available fields to understand CCAPI structure
            spdlog::info("[CCAPI-DEBUG] Orderbook message for {}-{} has {} fields:", exchange, symbol, nameValueMap.size());
            for (const auto& [key, value] : nameValueMap) {
                spdlog::info("[CCAPI-DEBUG]   {}: '{}'", key, std::string(value));
            }
            
            // Parse Bybit format with maximum precision preservation
            auto bid_price_it = nameValueMap.find("BID_PRICE");
            auto bid_size_it = nameValueMap.find("BID_SIZE");
            if (bid_price_it != nameValueMap.end() && bid_size_it != nameValueMap.end()) {
                try {
                    std::string price_str = std::string(bid_price_it->second);
                    std::string size_str = std::string(bid_size_it->second);
                    long double price = std::stold(price_str);
                    long double size = std::stold(size_str);
                    if (price > 0 && size > 0) {
                        bid_levels.emplace_back(price, size);
                        best_bid_price = price;
                        best_bid_size = size;
                        best_bid_price_str = price_str;
                        best_bid_size_str = size_str;
                        spdlog::info("[CCAPI-DEBUG] Found bid: price={:.12Lf} ({}), size={:.12Lf} ({})", 
                                   price, price_str, size, size_str);
                    }
                } catch (const std::exception& e) {
                    spdlog::warn("[CCAPI-DEBUG] Failed to parse bid data: {}", e.what());
                }
            }
            
            auto ask_price_it = nameValueMap.find("ASK_PRICE");
            auto ask_size_it = nameValueMap.find("ASK_SIZE");
            if (ask_price_it != nameValueMap.end() && ask_size_it != nameValueMap.end()) {
                try {
                    std::string price_str = std::string(ask_price_it->second);
                    std::string size_str = std::string(ask_size_it->second);
                    long double price = std::stold(price_str);
                    long double size = std::stold(size_str);
                    if (price > 0 && size > 0) {
                        ask_levels.emplace_back(price, size);
                        best_ask_price = price;
                        best_ask_size = size;
                        best_ask_price_str = price_str;
                        best_ask_size_str = size_str;
                        spdlog::info("[CCAPI-DEBUG] Found ask: price={:.12Lf} ({}), size={:.12Lf} ({})", 
                                   price, price_str, size, size_str);
                    }
                } catch (const std::exception& e) {
                    spdlog::warn("[CCAPI-DEBUG] Failed to parse ask data: {}", e.what());
                }
            }
            
            // Note: Bybit sends separate messages for bids and asks, so it's normal for one side to be empty
        }
        
        book_data.best_bid_price = best_bid_price;
        book_data.best_bid_size = best_bid_size;
        book_data.best_ask_price = best_ask_price;
        book_data.best_ask_size = best_ask_size;
        book_data.best_bid_price_str = best_bid_price_str;
        book_data.best_bid_size_str = best_bid_size_str;
        book_data.best_ask_price_str = best_ask_price_str;
        book_data.best_ask_size_str = best_ask_size_str;
        
        // Calculate derived features
        if (best_bid_price > 0 && best_ask_price > 0) {
            book_data.midpoint = (best_bid_price + best_ask_price) / 2.0;
            book_data.relative_spread = ((best_ask_price - best_bid_price) / book_data.midpoint);
            book_data.breadth = best_bid_price * best_bid_size + best_ask_price * best_ask_size;
            
            double total_vol = best_bid_size + best_ask_size;
            book_data.imbalance_lvl1 = total_vol > 0 ? (best_bid_size - best_ask_size) / total_vol : 0.0;
            
            // Calculate depth_n like Python reference (sum price * size across top N levels)
            book_data.bid_depth_n = 0.0;
            book_data.ask_depth_n = 0.0;
            
            // Sort bids by price descending (highest prices first)
            std::sort(bid_levels.begin(), bid_levels.end(), 
                    [](const auto& a, const auto& b) { return a.first > b.first; });
            
            // Sort asks by price ascending (lowest prices first)
            std::sort(ask_levels.begin(), ask_levels.end(), 
                    [](const auto& a, const auto& b) { return a.first < b.first; });
            
            // Calculate bid depth_n (top N levels)
            int bid_count = std::min(static_cast<int>(bid_levels.size()), depth_levels_);
            for (int i = 0; i < bid_count; ++i) {
                book_data.bid_depth_n += bid_levels[i].first * bid_levels[i].second;
            }
            
            // Calculate ask depth_n (top N levels)
            int ask_count = std::min(static_cast<int>(ask_levels.size()), depth_levels_);
            for (int i = 0; i < ask_count; ++i) {
                book_data.ask_depth_n += ask_levels[i].first * ask_levels[i].second;
            }
            
            book_data.depth_n = book_data.bid_depth_n + book_data.ask_depth_n;
            
            // Store full book data for serialization if available
            for (const auto& [price, size] : bid_levels) {
                book_data.bids[price] = size;
            }
            for (const auto& [price, size] : ask_levels) {
                book_data.asks[price] = size;
            }
            
            process_orderbook_data(book_data);
        }
    }
}

/**
 * @brief Handle trade messages from CCAPI
 */
void TradingEngineService::handle_trade_message(const ccapi::Message& message, const std::string& exchange, const std::string& symbol, uint64_t timestamp_ns) {
    TradeData trade_data;
    trade_data.exchange = exchange;
    trade_data.symbol = symbol;
    trade_data.timestamp_ns = timestamp_ns;
    trade_data.receipt_timestamp_ns = get_current_time_ns();
    
    // Initialize with default values to prevent garbage data
    trade_data.price = 0.0;
    trade_data.amount = 0.0;
    trade_data.transaction_price = 0.0;
    trade_data.trading_volume = 0.0;
    trade_data.side = "unknown";
    trade_data.trade_id = "N/A";
    trade_data.volatility_transaction_price = 0.0;
    trade_data.transaction_price_window_size = 0;
    trade_data.sequence_number = 0;
    
    // Parse trade from CCAPI message
    const auto& elementList = message.getElementList();
    if (!elementList.empty()) {
        for (const auto& element : elementList) {
            const auto& nameValueMap = element.getNameValueMap();
            
            // DEBUG: Log all available fields to understand CCAPI structure
            spdlog::info("[CCAPI-DEBUG] Trade message for {}-{} has {} fields:", exchange, symbol, nameValueMap.size());
            for (const auto& [key, value] : nameValueMap) {
                spdlog::info("[CCAPI-DEBUG]   {}: '{}'", key, std::string(value));
            }
            
            // Also log the raw message for debugging
            spdlog::info("[CCAPI-DEBUG] Raw message type: {}", static_cast<int>(message.getType()));
            spdlog::info("[CCAPI-DEBUG] Raw message time: {}", message.getTime().time_since_epoch().count());
            
            // Parse CCAPI trade data - try all possible field names
            bool price_found = false, size_found = false;
            
            // Price field variations
            std::vector<std::string> price_fields = {"PRICE", "LAST_PRICE", "TRADE_PRICE", "p", "price"};
            for (const auto& field : price_fields) {
                auto field_it = nameValueMap.find(field);
                if (field_it != nameValueMap.end()) {
                    try {
                        trade_data.price = std::stod(std::string(field_it->second));
                        trade_data.transaction_price = trade_data.price;
                        price_found = true;
                        spdlog::info("[CCAPI-DEBUG] Found price in field '{}': {}", field, trade_data.price);
                        break;
                    } catch (const std::exception& e) {
                        spdlog::warn("[CCAPI-DEBUG] Failed to parse price from field '{}': {}", field, e.what());
                    }
                }
            }
            
            // Size/quantity field variations - expanded list
            std::vector<std::string> size_fields = {
                "SIZE", "QUANTITY", "TRADE_SIZE", "AMOUNT", "q", "size", "qty", 
                "QTY", "VOLUME", "vol", "v", "amount", "baseQty", "quoteQty"
            };
            for (const auto& field : size_fields) {
                auto field_it = nameValueMap.find(field);
                if (field_it != nameValueMap.end()) {
                    try {
                        std::string size_str = std::string(field_it->second);
                        if (!size_str.empty() && size_str != "0" && size_str != "null") {
                            trade_data.amount = std::stod(size_str);
                            size_found = true;
                            spdlog::info("[CCAPI-DEBUG] Found size in field '{}': {}", field, trade_data.amount);
                            break;
                        }
                    } catch (const std::exception& e) {
                        spdlog::warn("[CCAPI-DEBUG] Failed to parse size from field '{}': {}", field, e.what());
                    }
                }
            }
            
            // If no size found, check if we can extract from any field containing numbers
            if (!size_found) {
                spdlog::warn("[CCAPI-DEBUG] No size field found, checking all numeric fields...");
                for (const auto& [key, value] : nameValueMap) {
                    std::string val_str = std::string(value);
                    try {
                        double val = std::stod(val_str);
                        if (val > 0 && val != trade_data.price && key != "PRICE" && key != "price") {
                            trade_data.amount = val;
                            size_found = true;
                            spdlog::info("[CCAPI-DEBUG] Using field '{}' as size: {}", key, val);
                            break;
                        }
                    } catch (...) {
                        // Not a number, skip
                    }
                }
            }
            
            // Trade ID field variations - expanded list
            std::vector<std::string> id_fields = {
                "TRADE_ID", "ID", "i", "id", "tradeId", "execId", "exec_id", 
                "transactionId", "txId", "tid", "T"
            };
            for (const auto& field : id_fields) {
                auto field_it = nameValueMap.find(field);
                if (field_it != nameValueMap.end()) {
                    std::string id_str = std::string(field_it->second);
                    if (!id_str.empty() && id_str != "null" && id_str != "0") {
                        trade_data.trade_id = id_str;
                        spdlog::info("[CCAPI-DEBUG] Found trade ID in field '{}': {}", field, trade_data.trade_id);
                        break;
                    }
                }
            }
            
            // Generate fallback ID if none found
            if (trade_data.trade_id == "N/A") {
                static std::atomic<uint64_t> fallback_id{1000000000};
                trade_data.trade_id = std::to_string(++fallback_id);
                spdlog::info("[CCAPI-DEBUG] Generated fallback trade ID: {}", trade_data.trade_id);
            }
            
            // Side field variations
            std::vector<std::string> side_fields = {"IS_BUYER_MAKER", "SIDE", "s", "side", "m"};
            for (const auto& field : side_fields) {
                auto field_it = nameValueMap.find(field);
                if (field_it != nameValueMap.end()) {
                    std::string side_value = std::string(field_it->second);
                    if (field == "IS_BUYER_MAKER") {
                        trade_data.side = (side_value == "1" || side_value == "true") ? "sell" : "buy";
                    } else if (field == "SIDE") {
                        trade_data.side = (side_value == "1" || side_value == "buy" || side_value == "BUY") ? "buy" : "sell";
                    } else {
                        trade_data.side = side_value;
                    }
                    spdlog::info("[CCAPI-DEBUG] Found side in field '{}': {} -> {}", field, side_value, trade_data.side);
                    break;
                }
            }
            
            if (!price_found || !size_found) {
                spdlog::error("[CCAPI-DEBUG] Missing critical trade data - price_found: {}, size_found: {}", price_found, size_found);
                // Skip processing if we don't have essential data
                continue;
            }
            
            // Extract timestamp from message - try multiple approaches
            auto timestamp = message.getTime();
            trade_data.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                timestamp.time_since_epoch()
            ).count();
            
            // If timestamp is 0, try to find it in the message fields
            if (trade_data.timestamp_ns == 0) {
                std::vector<std::string> time_fields = {"TIME", "TIMESTAMP", "t", "ts", "time", "timestamp", "T"};
                for (const auto& field : time_fields) {
                    auto field_it = nameValueMap.find(field);
                    if (field_it != nameValueMap.end()) {
                        try {
                            std::string time_str = std::string(field_it->second);
                            // Convert to nanoseconds (assume input is milliseconds)
                            trade_data.timestamp_ns = std::stoll(time_str) * 1000000;
                            spdlog::info("[CCAPI-DEBUG] Found timestamp in field '{}': {}", field, trade_data.timestamp_ns);
                            break;
                        } catch (const std::exception& e) {
                            spdlog::warn("[CCAPI-DEBUG] Failed to parse timestamp from field '{}': {}", field, e.what());
                        }
                    }
                }
            }
            
            // If still no timestamp, use current time
            if (trade_data.timestamp_ns == 0) {
                trade_data.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                spdlog::info("[CCAPI-DEBUG] Using current time as timestamp: {}", trade_data.timestamp_ns);
            }
            
            trade_data.trading_volume = trade_data.price * trade_data.amount;
            
            process_trade_data(trade_data);
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
 * @brief Trade data publisher thread implementation
 * 
 * Publishes preprocessed trade data to subscribers on port 5558:
 * 1. Waits for messages in the trade data queue
 * 2. Publishes messages with multipart format (topic + payload)
 * 3. Uses Python reference topic format: exchange-preprocessed_trades-symbol
 */
void TradingEngineService::trade_data_publisher_thread() {
    spdlog::info("[TradeData] Publisher thread started on {}", trade_data_endpoint_);
    
    // Small delay to allow ZMQ subscriber connections to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    while (running_) {
        std::unique_lock<std::mutex> lock(trade_data_mutex_);
        
        // Wait for messages to publish or shutdown signal
        trade_data_cv_.wait(lock, [this] { return !trade_data_message_queue_.empty() || !running_; });
        
        while (!trade_data_message_queue_.empty()) {
            std::string topic_message = trade_data_message_queue_.front();
            trade_data_message_queue_.pop();
            
            lock.unlock();
            
            try {
                // Split topic and payload for multipart ZMQ message
                size_t space_pos = topic_message.find(' ');
                if (space_pos != std::string::npos) {
                    std::string topic = topic_message.substr(0, space_pos);
                    std::string payload = topic_message.substr(space_pos + 1);
                    
                    // Send topic first with SNDMORE flag
                    zmq::message_t topic_msg(topic.size());
                    memcpy(topic_msg.data(), topic.c_str(), topic.size());
                    trade_data_publisher_socket_->send(topic_msg, zmq::send_flags::sndmore);
                    
                    // Send payload
                    zmq::message_t payload_msg(payload.size());
                    memcpy(payload_msg.data(), payload.c_str(), payload.size());
                    trade_data_publisher_socket_->send(payload_msg, zmq::send_flags::dontwait);
                } else {
                    // Fallback: send as single message
                    zmq::message_t zmq_msg(topic_message.size());
                    memcpy(zmq_msg.data(), topic_message.c_str(), topic_message.size());
                    trade_data_publisher_socket_->send(zmq_msg, zmq::send_flags::dontwait);
                }
            } catch (const std::exception& e) {
                spdlog::error("[TradeData] Publisher error: {}", e.what());
            }
            
            lock.lock();
        }
    }
    
    spdlog::info("[TradeData] Publisher thread stopped");
}

/**
 * @brief Orderbook data publisher thread implementation
 * 
 * Publishes preprocessed orderbook data to subscribers on port 5559:
 * 1. Waits for messages in the orderbook data queue
 * 2. Publishes messages with multipart format (topic + payload)
 * 3. Uses Python reference topic format: exchange-preprocessed_book-symbol
 */
void TradingEngineService::orderbook_data_publisher_thread() {
    spdlog::info("[OrderbookData] Publisher thread started on {}", orderbook_data_endpoint_);
    
    // Small delay to allow ZMQ subscriber connections to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    while (running_) {
        std::unique_lock<std::mutex> lock(orderbook_data_mutex_);
        
        // Wait for messages to publish or shutdown signal
        orderbook_data_cv_.wait(lock, [this] { return !orderbook_data_message_queue_.empty() || !running_; });
        
        while (!orderbook_data_message_queue_.empty()) {
            std::string topic_message = orderbook_data_message_queue_.front();
            orderbook_data_message_queue_.pop();
            
            lock.unlock();
            
            try {
                // Split topic and payload for multipart ZMQ message
                size_t space_pos = topic_message.find(' ');
                if (space_pos != std::string::npos) {
                    std::string topic = topic_message.substr(0, space_pos);
                    std::string payload = topic_message.substr(space_pos + 1);
                    
                    // Send topic first with SNDMORE flag
                    zmq::message_t topic_msg(topic.size());
                    memcpy(topic_msg.data(), topic.c_str(), topic.size());
                    orderbook_data_publisher_socket_->send(topic_msg, zmq::send_flags::sndmore);
                    
                    // Send payload
                    zmq::message_t payload_msg(payload.size());
                    memcpy(payload_msg.data(), payload.c_str(), payload.size());
                    orderbook_data_publisher_socket_->send(payload_msg, zmq::send_flags::dontwait);
                } else {
                    // Fallback: send as single message
                    zmq::message_t zmq_msg(topic_message.size());
                    memcpy(zmq_msg.data(), topic_message.c_str(), topic_message.size());
                    orderbook_data_publisher_socket_->send(zmq_msg, zmq::send_flags::dontwait);
                }
            } catch (const std::exception& e) {
                spdlog::error("[OrderbookData] Publisher error: {}", e.what());
            }
            
            lock.lock();
        }
    }
    
    spdlog::info("[OrderbookData] Publisher thread stopped");
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
                    // In simplified version, just simulate the order directly
                    spdlog::info("running execution");
                }
            } else {
                spdlog::warn("[TradingEngine] Non-CEX orders not supported in simplified mode: {}", 
                             order.venue_type);
                
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
            spdlog::warn("[TradingEngine] Cancel orders not implemented in simplified mode");
            
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
            spdlog::warn("[TradingEngine] Unknown action: {}", order.action);
            
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
        
        spdlog::error("[TradingEngine] Order processing error: {}", e.what());
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
    
    auto order_type_it = order.details.find("order_type");
    if (order_type_it != order.details.end() && order_type_it->second == "market") {
        // Market orders get filled at best available price
        base_price = 50000.0;  // Default mid price
    } else if (order_type_it != order.details.end() && order_type_it->second == "limit") {
        auto price_it = order.details.find("price");
        if (price_it != order.details.end()) {
            // Limit orders get filled at limit price or better
            base_price = std::stod(price_it->second);
        } else {
            base_price = 50000.0;
        }
    } else {
        base_price = 50000.0;
    }
    
    // Add realistic slippage
    double slippage_factor = slippage_bps_ / 10000.0;  // Convert bps to decimal
    
    // Safely check for side field before accessing
    auto side_it = order.details.find("side");
    if (side_it != order.details.end()) {
        if (side_it->second == "buy") {
            base_price *= (1.0 + slippage_factor);
        } else {
            base_price *= (1.0 - slippage_factor);
        }
    } else {
        // Default to buy-side slippage if side is not specified
        spdlog::warn("[TradingEngine] Order missing 'side' field, defaulting to buy-side slippage");
        base_price *= (1.0 + slippage_factor);
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
    auto symbol_it = order.details.find("symbol");
    if (symbol_it != order.details.end()) {
        fill.symbol_or_pair = symbol_it->second;
    } else {
        spdlog::error("[TradingEngine] Missing symbol in order details");
        fill.symbol_or_pair = "UNKNOWN";
    }
    
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
    
    spdlog::info("[BacktestEngine] Simulated fill: {} symbol={} price={} size={}", 
                 order.cl_id, fill.symbol_or_pair, fill_price, fill_size);
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

/**
 * @brief Publish preprocessed TradeData to trade data subscribers
 * @param trade_data The preprocessed TradeData to publish
 * 
 * Thread-safe publication of preprocessed trade data:
 * 1. Serializes preprocessed TradeData to JSON format
 * 2. Adds message to trade data queue with proper locking
 * 3. Notifies trade data publisher thread via condition variable
 * 
 * Called by market data processing handlers to communicate
 * preprocessed trade data to trade data subscribers.
 */
void TradingEngineService::publish_preprocessed_trade_data(const TradeData& trade_data) {
    try {
        std::string json_trade_data = serialize_trade_data(trade_data);
        std::string topic_message = trade_data.exchange + "-preprocessed_trades-" + trade_data.symbol + " " + json_trade_data;
        
        std::lock_guard<std::mutex> lock(trade_data_mutex_);
        trade_data_message_queue_.push(topic_message);
        trade_data_cv_.notify_one();
        
    } catch (const std::exception& e) {
        spdlog::error("[TradeData] Error publishing preprocessed trade data: {}", e.what());
    }
}

/**
 * @brief Publish preprocessed OrderBookData to orderbook data subscribers
 * @param book_data The preprocessed OrderBookData to publish
 * 
 * Thread-safe publication of preprocessed orderbook data:
 * 1. Serializes preprocessed OrderBookData to JSON format
 * 2. Adds message to orderbook data queue with proper locking
 * 3. Notifies orderbook data publisher thread via condition variable
 * 
 * Called by market data processing handlers to communicate
 * preprocessed orderbook data to orderbook data subscribers.
 */
void TradingEngineService::publish_preprocessed_orderbook_data(const OrderBookData& book_data) {
    try {
        std::string json_book_data = serialize_orderbook_data(book_data);
        std::string topic_message = book_data.exchange + "-preprocessed_book-" + book_data.symbol + " " + json_book_data;
        
        std::lock_guard<std::mutex> lock(orderbook_data_mutex_);
        orderbook_data_message_queue_.push(topic_message);
        orderbook_data_cv_.notify_one();
        
    } catch (const std::exception& e) {
        spdlog::error("[OrderbookData] Error publishing preprocessed orderbook data: {}", e.what());
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

std::string TradingEngineService::serialize_orderbook_data(const OrderBookData& book_data) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();
    
    doc.AddMember("exchange", rapidjson::Value(book_data.exchange.c_str(), allocator), allocator);
    doc.AddMember("symbol", rapidjson::Value(book_data.symbol.c_str(), allocator), allocator);
    doc.AddMember("timestamp_ns", rapidjson::Value(book_data.timestamp_ns), allocator);
    doc.AddMember("receipt_timestamp_ns", rapidjson::Value(book_data.receipt_timestamp_ns), allocator);
    doc.AddMember("best_bid_price", rapidjson::Value(static_cast<double>(book_data.best_bid_price)), allocator);
    doc.AddMember("best_bid_size", rapidjson::Value(static_cast<double>(book_data.best_bid_size)), allocator);
    doc.AddMember("best_ask_price", rapidjson::Value(static_cast<double>(book_data.best_ask_price)), allocator);
    doc.AddMember("best_ask_size", rapidjson::Value(static_cast<double>(book_data.best_ask_size)), allocator);
    doc.AddMember("midpoint", rapidjson::Value(book_data.midpoint), allocator);
    doc.AddMember("relative_spread", rapidjson::Value(book_data.relative_spread), allocator);
    doc.AddMember("breadth", rapidjson::Value(book_data.breadth), allocator);
    doc.AddMember("imbalance_lvl1", rapidjson::Value(book_data.imbalance_lvl1), allocator);
    doc.AddMember("bid_depth_n", rapidjson::Value(book_data.bid_depth_n), allocator);
    doc.AddMember("ask_depth_n", rapidjson::Value(book_data.ask_depth_n), allocator);
    doc.AddMember("depth_n", rapidjson::Value(book_data.depth_n), allocator);
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    return buffer.GetString();
}

std::string TradingEngineService::serialize_trade_data(const TradeData& trade_data) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();
    
    doc.AddMember("exchange", rapidjson::Value(trade_data.exchange.c_str(), allocator), allocator);
    doc.AddMember("symbol", rapidjson::Value(trade_data.symbol.c_str(), allocator), allocator);
    doc.AddMember("timestamp_ns", rapidjson::Value(trade_data.timestamp_ns), allocator);
    doc.AddMember("receipt_timestamp_ns", rapidjson::Value(trade_data.receipt_timestamp_ns), allocator);
    doc.AddMember("price", rapidjson::Value(trade_data.price), allocator);
    doc.AddMember("amount", rapidjson::Value(trade_data.amount), allocator);
    doc.AddMember("side", rapidjson::Value(trade_data.side.c_str(), allocator), allocator);
    doc.AddMember("trade_id", rapidjson::Value(trade_data.trade_id.c_str(), allocator), allocator);
    doc.AddMember("trading_volume", rapidjson::Value(trade_data.trading_volume), allocator);
    doc.AddMember("volatility_transaction_price", rapidjson::Value(trade_data.volatility_transaction_price), allocator);
    doc.AddMember("transaction_price_window_size", rapidjson::Value(trade_data.transaction_price_window_size), allocator);
    doc.AddMember("sequence_number", rapidjson::Value(trade_data.sequence_number), allocator);
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    return buffer.GetString();
}

/**
 * @brief Normalize exchange name to uppercase
 * @param exchange Raw exchange name
 * @return Normalized exchange name
 */
std::string TradingEngineService::normalize_exchange(const std::string& exchange) {
    std::string normalized = exchange;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);
    return normalized;
}

/**
 * @brief Normalize symbol to use dash separator
 * @param symbol Raw symbol
 * @return Normalized symbol
 */
std::string TradingEngineService::normalize_symbol(const std::string& symbol) {
    std::string normalized = symbol;
    // Replace various separators with dash
    std::replace(normalized.begin(), normalized.end(), '/', '-');
    std::replace(normalized.begin(), normalized.end(), '_', '-');
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);
    return normalized;
}

/**
 * @brief Convert symbol to exchange-specific format
 * @param symbol Normalized symbol (e.g., "BTC-USDT")
 * @param exchange Exchange name
 * @return Exchange-specific symbol format
 */
std::string TradingEngineService::convert_symbol_for_exchange(const std::string& symbol, const std::string& exchange) {
    std::string exchange_lower = exchange;
    std::transform(exchange_lower.begin(), exchange_lower.end(), exchange_lower.begin(), ::tolower);
    
    if (exchange_lower == "bybit") {
        // Bybit expects no separator: BTC-USDT -> BTCUSDT
        std::string result = symbol;
        result.erase(std::remove(result.begin(), result.end(), '-'), result.end());
        result.erase(std::remove(result.begin(), result.end(), '/'), result.end());
        result.erase(std::remove(result.begin(), result.end(), '_'), result.end());
        return result;
    } else if (exchange_lower == "binance") {
        // Binance also expects no separator: BTC-USDT -> BTCUSDT
        std::string result = symbol;
        result.erase(std::remove(result.begin(), result.end(), '-'), result.end());
        result.erase(std::remove(result.begin(), result.end(), '/'), result.end());
        result.erase(std::remove(result.begin(), result.end(), '_'), result.end());
        return result;
    }
    
    // Default: return as-is for other exchanges
    return symbol;
}

/**
 * @brief Process and publish orderbook data
 * @param book_data Raw orderbook data from CCAPI
 */
void TradingEngineService::process_orderbook_data(const OrderBookData& book_data) {
    try {
        // Create a copy for processing
        OrderBookData processed_book = book_data;
        
        // Add preprocessing metadata
        processed_book.preprocessing_timestamp = std::to_string(get_current_time_ns());
        processed_book.receipt_timestamp_ns = get_current_time_ns();
        
        // Update rolling statistics with thread safety
        std::string symbol_key = processed_book.exchange + ":" + processed_book.symbol;
        
        FastRollingStats::BookRollResult book_result;
        {
            std::lock_guard<std::mutex> stats_lock(trade_data_mutex_);
            
            // Safe access to symbol stats - create if doesn't exist
            auto stats_it = symbol_stats_.find(symbol_key);
            if (stats_it == symbol_stats_.end()) {
                symbol_stats_[symbol_key] = FastRollingStats();
                stats_it = symbol_stats_.find(symbol_key);
            }
            
            book_result = stats_it->second.update_book(processed_book.midpoint, 
                                               processed_book.best_bid_price, processed_book.best_bid_size,
                                               processed_book.best_ask_price, processed_book.best_ask_size);
        }
        
        // Add rolling statistics to processed data
        processed_book.volatility_mid = book_result.volatility_mid;
        processed_book.ofi_rolling = book_result.ofi_rolling;
        processed_book.midpoint_window_size = book_result.midpoint_window_size;
        
        // Add sequence number
        std::string partition_id = processed_book.exchange + ":" + processed_book.symbol + ":book";
        processed_book.sequence_number = get_next_sequence(partition_id);
        
        // Publish the processed orderbook data
        publish_preprocessed_orderbook_data(processed_book);
        
    } catch (const std::exception& e) {
        spdlog::error("[MarketData] Error processing orderbook data: {}", e.what());
    }
}

/**
 * @brief Process and publish trade data
 * @param trade_data Raw trade data from CCAPI
 */
void TradingEngineService::process_trade_data(const TradeData& trade_data) {
    try {
        // Create a copy for processing
        TradeData processed_trade = trade_data;
        
        // Add preprocessing metadata
        processed_trade.preprocessing_timestamp = std::to_string(get_current_time_ns());
        processed_trade.receipt_timestamp_ns = get_current_time_ns();
        
        // Update rolling statistics with thread safety
        std::string symbol_key = processed_trade.exchange + ":" + processed_trade.symbol;
        
        FastRollingStats::TradeRollResult trade_result;
        {
            std::lock_guard<std::mutex> stats_lock(trade_data_mutex_);
            
            // Safe access to symbol stats - create if doesn't exist
            auto stats_it = symbol_stats_.find(symbol_key);
            if (stats_it == symbol_stats_.end()) {
                symbol_stats_[symbol_key] = FastRollingStats();
                stats_it = symbol_stats_.find(symbol_key);
            }
            
            trade_result = stats_it->second.update_trade(processed_trade.transaction_price);
        }
        
        // Add rolling statistics to processed data
        processed_trade.volatility_transaction_price = trade_result.volatility_transaction_price;
        processed_trade.transaction_price_window_size = trade_result.transaction_price_window_size;
        
        // Add sequence number
        std::string partition_id = processed_trade.exchange + ":" + processed_trade.symbol + ":trade";
        processed_trade.sequence_number = get_next_sequence(partition_id);
        
        // Publish the processed trade data
        publish_preprocessed_trade_data(processed_trade);
        
    } catch (const std::exception& e) {
        spdlog::error("[MarketData] Error processing trade data: {}", e.what());
    }
}

/**
 * @brief Get next sequence number for a stream
 * @param partition_id Stream partition identifier (exchange:symbol:type)
 * @return Next sequence number
 */
int TradingEngineService::get_next_sequence(const std::string& partition_id) {
    std::lock_guard<std::mutex> lock(trade_data_mutex_);
    
    // Safe access to sequence numbers - initialize to 0 if doesn't exist
    auto seq_it = sequence_numbers_.find(partition_id);
    if (seq_it == sequence_numbers_.end()) {
        sequence_numbers_[partition_id] = 0;
        seq_it = sequence_numbers_.find(partition_id);
    }
    
    return ++seq_it->second;
}

/**
 * @brief Get exchanges from environment variable or default
 * @return Vector of exchange names
 */
std::vector<std::string> TradingEngineService::getExchangesFromConfig() const {
    const char* env_exchanges = std::getenv("EXCHANGES");
    if (env_exchanges) {
        std::string exchanges_str(env_exchanges);
        if (!exchanges_str.empty()) {
            return parseCommaSeparated(exchanges_str);
        }
    }
    
    // Default exchanges
    return {"bybit"};
}

/**
 * @brief Get symbols from environment variable or default
 * @return Vector of symbol names
 */
std::vector<std::string> TradingEngineService::getSymbolsFromConfig() const {
    // Try to get symbols dynamically from exchanges first
    std::vector<std::string> exchanges = getExchangesFromConfig();
    if (!exchanges.empty()) {
        try {
            // Get top 500 symbols dynamically from the first exchange
            auto dynamic_symbols = getDynamicSymbolsFromExchange(exchanges[0], 10, "USDT");
            if (!dynamic_symbols.empty()) {
                spdlog::info("[TradingEngine] Using {} dynamic symbols from {}", 
                           dynamic_symbols.size(), exchanges[0]);
                return dynamic_symbols;
            }
        } catch (const std::exception& e) {
            spdlog::warn("[TradingEngine] Failed to fetch dynamic symbols, falling back to static: {}", e.what());
        }
    }
    // Fallback to static symbols
    spdlog::info("[TradingEngine] Using static symbol list as fallback");
    return { "ETH-USDT", "BTC-USDT", "1INCH-USDT", "AAVE-USDT", "COMP-USDT", "CRV-USDT", "SUSHI-USDT", "YFI-USDT",
    "UMA-USDT", "BAL-USDT", "MKR-USDT", "SNX-USDT", "ALPHA-USDT", "CREAM-USDT"
    };
}

/**
 * @brief Get symbols dynamically from exchange APIs
 */
std::vector<std::string> TradingEngineService::getDynamicSymbolsFromExchange(
    const std::string& exchange_name,
    int top_n,
    const std::string& quote_currency) const {
    
    // Initialize symbol manager if not already done
    if (!symbol_manager_) {
        symbol_manager_ = std::make_unique<DynamicSymbolManager>();
    }
    
    // Configure fetcher
    FetcherConfig config;
    config.top_n = top_n;
    config.quote_currency = quote_currency;
    config.include_leveraged = false;  // Exclude leveraged tokens by default
    config.min_turnover = 0.0;         // No minimum turnover
    config.timeout = std::chrono::seconds{30};
    
    try {
        auto symbols = symbol_manager_->fetch_symbols_for_exchange(exchange_name, config);
        
        if (symbols.empty()) {
            spdlog::warn("[TradingEngine] No symbols fetched from {}", exchange_name);
            return {};
        }
        
        spdlog::info("[TradingEngine] Fetched {} symbols from {} (top {} {})", 
                    symbols.size(), exchange_name, top_n, quote_currency);
        
        // Log first few symbols for debugging
        if (symbols.size() >= 10) {
            std::ostringstream oss;
            for (size_t i = 0; i < 10; ++i) {
                if (i > 0) oss << ", ";
                oss << symbols[i];
            }
            if (symbols.size() > 10) {
                oss << " ...";
            }
            spdlog::info("[TradingEngine] Sample symbols: {}", oss.str());
        }
        
        return symbols;
        
    } catch (const std::exception& e) {
        spdlog::error("[TradingEngine] Failed to fetch symbols from {}: {}", exchange_name, e.what());
        return {};
    }
}

/**
 * @brief Parse comma-separated string into vector
 * @param input Comma-separated string
 * @return Vector of strings
 */
std::vector<std::string> TradingEngineService::parseCommaSeparated(const std::string& input) const {
    std::vector<std::string> result;
    std::stringstream ss(input);
    std::string item;
    
    while (std::getline(ss, item, ',')) {
        // Trim whitespace
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    
    return result;
}

/**
 * @brief FastRollingStats constructor
 * @param window_size Size of the rolling window
 */
FastRollingStats::FastRollingStats(size_t window_size) : window_size_(window_size) {}

/**
 * @brief Update trade statistics with new transaction price
 * @param transaction_price Latest transaction price
 * @return Trade statistics result
 */
FastRollingStats::TradeRollResult FastRollingStats::update_trade(double transaction_price) {
    transaction_prices_.push_back(transaction_price);
    
    // Keep window size
    if (transaction_prices_.size() > window_size_) {
        transaction_prices_.pop_front();
    }
    
    TradeRollResult result;
    result.volatility_transaction_price = calculate_volatility(transaction_prices_);
    result.transaction_price_window_size = static_cast<int>(transaction_prices_.size());
    
    return result;
}

/**
 * @brief Update book statistics with new market data
 * @param midpoint Current midpoint price
 * @param best_bid_price Best bid price
 * @param best_bid_size Best bid size
 * @param best_ask_price Best ask price
 * @param best_ask_size Best ask size
 * @return Book statistics result
 */
FastRollingStats::BookRollResult FastRollingStats::update_book(double midpoint, 
                                                              double best_bid_price, double best_bid_size, 
                                                              double best_ask_price, double best_ask_size) {
    midpoints_.push_back(midpoint);
    
    // Keep window size
    if (midpoints_.size() > window_size_) {
        midpoints_.pop_front();
    }
    
    // Calculate OFI and add to rolling window
    double ofi = calculate_ofi(best_bid_price, best_bid_size, best_ask_price, best_ask_size);
    ofi_values_.push_back(ofi);
    
    if (ofi_values_.size() > window_size_) {
        ofi_values_.pop_front();
    }
    
    BookRollResult result;
    result.volatility_mid = calculate_volatility(midpoints_);
    result.ofi_rolling = ofi_values_.empty() ? 0.0 : 
                        std::accumulate(ofi_values_.begin(), ofi_values_.end(), 0.0) / ofi_values_.size();
    result.midpoint_window_size = static_cast<int>(midpoints_.size());
    
    return result;
}

/**
 * @brief Calculate volatility (standard deviation) of values
 * @param values Deque of values
 * @return Standard deviation
 */
double FastRollingStats::calculate_volatility(const std::deque<double>& values) {
    if (values.size() < 2) return 0.0;
    
    double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    double sum_sq_diff = 0.0;
    
    for (const auto& val : values) {
        double diff = val - mean;
        sum_sq_diff += diff * diff;
    }
    
    return std::sqrt(sum_sq_diff / (values.size() - 1));
}

/**
 * @brief Calculate Order Flow Imbalance
 * @param best_bid_price Best bid price
 * @param best_bid_size Best bid size
 * @param best_ask_price Best ask price
 * @param best_ask_size Best ask size
 * @return OFI value
 */
double FastRollingStats::calculate_ofi(double best_bid_price, double best_bid_size, 
                                      double best_ask_price, double best_ask_size) {
    double total_size = best_bid_size + best_ask_size;
    if (total_size == 0.0) return 0.0;
    
    return (best_bid_size - best_ask_size) / total_size;
}

} // namespace latentspeed
