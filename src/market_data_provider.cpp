/**
 * @file market_data_provider.cpp
 * @brief Ultra-low latency market data provider implementation
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#include "market_data_provider.h"
#include <spdlog/spdlog.h>
#include <rapidjson/error/en.h>
#include <iomanip>
#include <sstream>
#include <chrono>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace latentspeed {

MarketDataProvider::MarketDataProvider(const std::string& exchange, 
                                     const std::vector<std::string>& symbols)
    : exchange_(exchange)
    , symbols_(symbols)
    , running_(false) {
    
    spdlog::info("[MarketData] Initializing provider for exchange: {}", exchange_);
    
    // Initialize memory pools
    tick_pool_ = std::make_unique<hft::MemoryPool<MarketTick, 1024>>();
    orderbook_pool_ = std::make_unique<hft::MemoryPool<OrderBookSnapshot, 512>>();
    
    // Initialize lock-free queues
    tick_queue_ = std::make_unique<hft::LockFreeSPSCQueue<MarketTick, 4096>>();
    orderbook_queue_ = std::make_unique<hft::LockFreeSPSCQueue<OrderBookSnapshot, 2048>>();
    // Initialize raw message queue from WebSocket (fixed-size buffer for lock-free queue)
    message_queue_ = std::make_unique<hft::LockFreeSPSCQueue<MessageBuffer, 8192>>();
    
    spdlog::info("[MarketData] Memory pools and queues initialized");
}

MarketDataProvider::~MarketDataProvider() {
    stop();
}

bool MarketDataProvider::initialize() {
    try {
        spdlog::info("[MarketData] Initializing ZMQ context and publishers...");
        
        // Initialize ZMQ context
        zmq_context_ = std::make_unique<zmq::context_t>(1);
        
        // Initialize trades publisher (port 5558)
        trades_publisher_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PUB);
        trades_publisher_->set(zmq::sockopt::sndhwm, 1000);
        trades_publisher_->set(zmq::sockopt::sndtimeo, 0);
        trades_publisher_->bind("tcp://*:5558");
        
        // Initialize orderbook publisher (port 5559)
        orderbook_publisher_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PUB);
        orderbook_publisher_->set(zmq::sockopt::sndhwm, 1000);
        orderbook_publisher_->set(zmq::sockopt::sndtimeo, 0);
        orderbook_publisher_->bind("tcp://*:5559");
        
        // Initialize Boost.Beast WebSocket components
        io_context_ = std::make_unique<boost::asio::io_context>();
        ssl_context_ = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12_client);
        
        // Configure SSL context
        ssl_context_->set_default_verify_paths();
        ssl_context_->set_verify_mode(boost::asio::ssl::verify_peer);
        
        spdlog::info("[MarketData] ZMQ publishers bound to ports 5558 (trades) and 5559 (orderbook)");
        spdlog::info("[MarketData] WebSocket client initialized");
        
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[MarketData] Initialization failed: {}", e.what());
        return false;
    }
}

void MarketDataProvider::start() {
    if (running_.exchange(true)) {
        spdlog::warn("[MarketData] Already running");
        return;
    }
    
    spdlog::info("[MarketData] Starting market data provider...");
    
    // Start WebSocket thread
    ws_thread_ = std::make_unique<std::thread>(&MarketDataProvider::websocket_thread, this);
    
    // Start processing thread
    processing_thread_ = std::make_unique<std::thread>(&MarketDataProvider::processing_thread, this);
    
    // Start publishing thread
    publishing_thread_ = std::make_unique<std::thread>(&MarketDataProvider::publishing_thread, this);
    
    // Set thread priorities and CPU affinity (Linux-specific)
#ifdef __linux__
    struct sched_param param;
    param.sched_priority = 50;
    
    // Set real-time priority for processing thread
    if (pthread_setschedparam(processing_thread_->native_handle(), SCHED_FIFO, &param) != 0) {
        spdlog::warn("[MarketData] Failed to set real-time scheduling for processing thread");
    }
    
    // CPU affinity
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(6, &cpuset); // Use CPU core 6 for WebSocket
    pthread_setaffinity_np(ws_thread_->native_handle(), sizeof(cpu_set_t), &cpuset);
    
    CPU_ZERO(&cpuset);
    CPU_SET(7, &cpuset); // Use CPU core 7 for processing
    pthread_setaffinity_np(processing_thread_->native_handle(), sizeof(cpu_set_t), &cpuset);
#endif
    
    spdlog::info("[MarketData] Market data provider started with {} symbols", symbols_.size());
}

void MarketDataProvider::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    spdlog::info("[MarketData] Stopping market data provider...");
    
    // Stop WebSocket connection
    if (ws_stream_) {
        try {
            ws_stream_->close(boost::beast::websocket::close_code::normal);
        } catch (const std::exception& e) {
            spdlog::warn("[MarketData] Error closing WebSocket: {}", e.what());
        }
    }
    
    if (io_context_) {
        io_context_->stop();
    }
    
    // Join threads
    if (ws_thread_ && ws_thread_->joinable()) {
        ws_thread_->join();
    }
    if (processing_thread_ && processing_thread_->joinable()) {
        processing_thread_->join();
    }
    if (publishing_thread_ && publishing_thread_->joinable()) {
        publishing_thread_->join();
    }
    
    spdlog::info("[MarketData] Market data provider stopped");
    spdlog::info("[MarketData] Final stats - Trades: {}, OrderBooks: {}, Published: {}, Errors: {}",
                 stats_.trades_processed.load(),
                 stats_.orderbooks_processed.load(),
                 stats_.messages_published.load(),
                 stats_.errors.load());
}

void MarketDataProvider::set_callbacks(std::shared_ptr<MarketDataCallbacks> callbacks) {
    callbacks_ = callbacks;
}

void MarketDataProvider::websocket_thread() {
    spdlog::info("[MarketData] WebSocket thread started");
    
    try {
        connect_websocket();
        
        // Run the I/O context
        io_context_->run();
        
    } catch (const std::exception& e) {
        spdlog::error("[MarketData] WebSocket thread error: {}", e.what());
        stats_.errors.fetch_add(1);
        if (callbacks_) {
            callbacks_->on_error("WebSocket error: " + std::string(e.what()));
        }
    }
    
    spdlog::info("[MarketData] WebSocket thread stopped");
}

void MarketDataProvider::connect_websocket() {
    std::string host, port, target;
    
    // Exchange-specific WebSocket URLs
    if (exchange_ == "bybit") {
        host = "stream.bybit.com";
        port = "443";
        target = "/v5/public/spot";
    } else if (exchange_ == "binance") {
        host = "stream.binance.com";
        port = "9443";
        target = "/ws";
    } else {
        throw std::runtime_error("Unsupported exchange: " + exchange_);
    }
    
    spdlog::info("[MarketData] Connecting to WebSocket: wss://{}:{}{}", host, port, target);
    
    // Create WebSocket stream
    ws_stream_ = std::make_unique<WSStream>(*io_context_, *ssl_context_);
    
    // Set SNI Hostname
    if (!SSL_set_tlsext_host_name(ws_stream_->next_layer().native_handle(), host.c_str())) {
        throw std::runtime_error("Failed to set SNI hostname");
    }
    
    // Resolve hostname
    boost::asio::ip::tcp::resolver resolver(*io_context_);
    auto const results = resolver.resolve(host, port);
    
    // Connect to server
    auto ep = boost::beast::get_lowest_layer(*ws_stream_).connect(results);
    
    // Update the host string for HTTP/1.1 header
    host += ":" + std::to_string(ep.port());
    
    // Perform SSL handshake
    ws_stream_->next_layer().handshake(boost::asio::ssl::stream_base::client);
    
    // Set WebSocket options
    ws_stream_->set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::client));
    ws_stream_->set_option(boost::beast::websocket::stream_base::decorator([](boost::beast::websocket::request_type& req) {
        req.set(boost::beast::http::field::user_agent, "Latentspeed/1.0");
    }));
    
    // Perform WebSocket handshake
    spdlog::info("[MarketData] Performing WebSocket handshake...");
    ws_stream_->handshake(host, target);
    spdlog::info("[MarketData] WebSocket handshake completed");
    
    // Send subscription
    spdlog::info("[MarketData] Sending subscription message...");
    send_subscription();
    spdlog::info("[MarketData] Starting message read loop...");
    
    // Start reading messages
    int message_count = 0;
    while (running_.load()) {
        try {
            ws_buffer_.clear();
            ws_stream_->read(ws_buffer_);
            message_count++;
            
            std::string message = boost::beast::buffers_to_string(ws_buffer_.data());
            spdlog::debug("[MarketData] Received message #{}: {}", message_count, message.substr(0, 200));
            
            // Copy message to fixed-size buffer for lock-free queue
            MessageBuffer msg_buffer;
            std::memcpy(msg_buffer.data(), message.c_str(), 
                       std::min(message.size(), msg_buffer.size() - 1));
            msg_buffer[std::min(message.size(), msg_buffer.size() - 1)] = '\0';
            
            // Push to processing queue
            if (!message_queue_->try_push(msg_buffer)) {
                spdlog::warn("[MarketData] Message queue full, dropping message");
                stats_.errors.fetch_add(1);
            }
            
        } catch (const boost::beast::system_error& se) {
            if (se.code() != boost::beast::websocket::error::closed) {
                spdlog::error("[MarketData] WebSocket read error: {}", se.what());
                stats_.errors.fetch_add(1);
            }
            break;
        }
    }
}

void MarketDataProvider::processing_thread() {
    spdlog::info("[MarketData] Processing thread started");
    
    MessageBuffer message;
    
    while (running_.load(std::memory_order_acquire)) {
        try {
            if (message_queue_->try_pop(message)) {
                // Convert buffer back to string
                std::string message_str(message.data());
                message_str = message_str.substr(0, message_str.find('\0')); // Remove null terminators
                
                // Parse message based on exchange
                if (exchange_ == "bybit") {
                    parse_bybit_message(message_str);
                } else if (exchange_ == "binance") {
                    parse_binance_message(message_str);
                }
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        } catch (const std::exception& e) {
            spdlog::error("[MarketData] Processing error: {}", e.what());
            stats_.errors.fetch_add(1);
            if (callbacks_) {
                callbacks_->on_error("Processing error: " + std::string(e.what()));
            }
        }
    }
    
    spdlog::info("[MarketData] Processing thread stopped");
}

void MarketDataProvider::publishing_thread() {
    spdlog::info("[MarketData] Publishing thread started");
    
    MarketTick tick;
    OrderBookSnapshot snapshot;
    
    while (running_.load(std::memory_order_acquire)) {
        try {
            // Process trade ticks
            if (tick_queue_->try_pop(tick)) {
                publish_trade(tick);
                if (callbacks_) {
                    callbacks_->on_trade(tick);
                }
            }
            
            // Process orderbook snapshots
            if (orderbook_queue_->try_pop(snapshot)) {
                publish_orderbook(snapshot);
                if (callbacks_) {
                    callbacks_->on_orderbook(snapshot);
                }
            }
            
            // Small yield if no data
            if (tick_queue_->empty() && orderbook_queue_->empty()) {
                std::this_thread::yield();
            }
        } catch (const std::exception& e) {
            spdlog::error("[MarketData] Publishing error: {}", e.what());
            stats_.errors.fetch_add(1);
        }
    }
    
    spdlog::info("[MarketData] Publishing thread stopped");
}

void MarketDataProvider::send_subscription() {
    std::string sub_msg = build_subscription_message();
    
    spdlog::info("[MarketData] Subscription message: {}", sub_msg);
    
    try {
        size_t bytes_written = ws_stream_->write(boost::asio::buffer(sub_msg));
        spdlog::info("[MarketData] Subscription sent successfully ({} bytes)", bytes_written);
    } catch (const std::exception& e) {
        spdlog::error("[MarketData] Failed to send subscription: {}", e.what());
        throw;
    }
}

void MarketDataProvider::handle_websocket_message(const std::string& message) {
    // Push to processing queue (this method is now replaced by inline code in connect_websocket)
    // Keeping for compatibility with other parts of the codebase
}

void MarketDataProvider::parse_bybit_message(const std::string& message) {
    try {
        rapidjson::Document doc;
        doc.Parse(message.c_str());
        
        if (doc.HasParseError()) {
            spdlog::warn("[MarketData] JSON parse error: {}", rapidjson::GetParseError_En(doc.GetParseError()));
            return;
        }
        
        // Check for topic field to determine message type
        if (!doc.HasMember("topic") || !doc["topic"].IsString()) {
            return; // Skip non-data messages
        }
        
        std::string topic = doc["topic"].GetString();
        
        // Parse trade data
        if (topic.find("publicTrade") != std::string::npos) {
            if (doc.HasMember("data") && doc["data"].IsArray()) {
                for (const auto& trade_data : doc["data"].GetArray()) {
                    MarketTick tick;
                    if (parse_trade_data(trade_data, tick)) {
                        if (!tick_queue_->try_push(tick)) {
                            spdlog::warn("[MarketData] Tick queue full");
                        } else {
                            stats_.trades_processed.fetch_add(1);
                        }
                    }
                }
            }
        }
        // Parse orderbook data
        else if (topic.find("orderbook") != std::string::npos) {
            if (doc.HasMember("data")) {
                OrderBookSnapshot snapshot;
                if (parse_orderbook_data(doc["data"], snapshot)) {
                    if (!orderbook_queue_->try_push(snapshot)) {
                        spdlog::warn("[MarketData] OrderBook queue full");
                    } else {
                        stats_.orderbooks_processed.fetch_add(1);
                    }
                }
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("[MarketData] Bybit parse error: {}", e.what());
        stats_.errors.fetch_add(1);
    }
}

void MarketDataProvider::parse_binance_message(const std::string& message) {
    try {
        rapidjson::Document doc;
        doc.Parse(message.c_str());
        
        if (doc.HasParseError()) {
            spdlog::warn("[MarketData] JSON parse error: {}", rapidjson::GetParseError_En(doc.GetParseError()));
            return;
        }
        
        // Binance trade stream format
        if (doc.HasMember("e") && doc["e"].IsString()) {
            std::string event_type = doc["e"].GetString();
            
            if (event_type == "trade") {
                MarketTick tick;
                if (parse_trade_data(doc, tick)) {
                    if (!tick_queue_->try_push(tick)) {
                        spdlog::warn("[MarketData] Tick queue full");
                    } else {
                        stats_.trades_processed.fetch_add(1);
                    }
                }
            } else if (event_type == "depthUpdate") {
                OrderBookSnapshot snapshot;
                if (parse_orderbook_data(doc, snapshot)) {
                    if (!orderbook_queue_->try_push(snapshot)) {
                        spdlog::warn("[MarketData] OrderBook queue full");
                    } else {
                        stats_.orderbooks_processed.fetch_add(1);
                    }
                }
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("[MarketData] Binance parse error: {}", e.what());
        stats_.errors.fetch_add(1);
    }
}

bool MarketDataProvider::parse_trade_data(const rapidjson::Value& doc, MarketTick& tick) {
    try {
        tick.timestamp_ns = get_timestamp_ns();
        tick.exchange.assign(exchange_);
        
        if (exchange_ == "bybit") {
            // Bybit trade format
            if (doc.HasMember("S") && doc["S"].IsString()) {
                tick.symbol.assign(doc["S"].GetString());
            }
            if (doc.HasMember("p") && doc["p"].IsString()) {
                tick.price = std::stod(doc["p"].GetString());
            }
            if (doc.HasMember("v") && doc["v"].IsString()) {
                tick.quantity = std::stod(doc["v"].GetString());
            }
            if (doc.HasMember("S") && doc["S"].IsString()) {
                tick.side.assign(doc["S"].GetString());
            }
            if (doc.HasMember("i") && doc["i"].IsString()) {
                tick.trade_id.assign(doc["i"].GetString());
            }
        } else if (exchange_ == "binance") {
            // Binance trade format
            if (doc.HasMember("s") && doc["s"].IsString()) {
                tick.symbol.assign(doc["s"].GetString());
            }
            if (doc.HasMember("p") && doc["p"].IsString()) {
                tick.price = std::stod(doc["p"].GetString());
            }
            if (doc.HasMember("q") && doc["q"].IsString()) {
                tick.quantity = std::stod(doc["q"].GetString());
            }
            if (doc.HasMember("m") && doc["m"].IsBool()) {
                tick.side.assign(doc["m"].GetBool() ? "sell" : "buy");
            }
            if (doc.HasMember("t") && doc["t"].IsUint64()) {
                tick.trade_id.assign(std::to_string(doc["t"].GetUint64()));
            }
        }
        
        return !tick.symbol.empty() && tick.price > 0 && tick.quantity > 0;
        
    } catch (const std::exception& e) {
        spdlog::warn("[MarketData] Trade parse error: {}", e.what());
        return false;
    }
}

bool MarketDataProvider::parse_orderbook_data(const rapidjson::Value& doc, OrderBookSnapshot& snapshot) {
    try {
        snapshot.timestamp_ns = get_timestamp_ns();
        snapshot.exchange.assign(exchange_);
        
        if (exchange_ == "bybit") {
            // Bybit orderbook format
            if (doc.HasMember("s") && doc["s"].IsString()) {
                snapshot.symbol.assign(doc["s"].GetString());
            }
            
            // Parse bids
            if (doc.HasMember("b") && doc["b"].IsArray()) {
                const auto& bids = doc["b"].GetArray();
                size_t count = std::min(static_cast<size_t>(bids.Size()), static_cast<size_t>(10));
                for (size_t i = 0; i < count; ++i) {
                    if (bids[i].IsArray() && bids[i].GetArray().Size() >= 2) {
                        const auto& level = bids[i].GetArray();
                        snapshot.bids[i].price = std::stod(level[0].GetString());
                        snapshot.bids[i].quantity = std::stod(level[1].GetString());
                    }
                }
            }
            
            // Parse asks
            if (doc.HasMember("a") && doc["a"].IsArray()) {
                const auto& asks = doc["a"].GetArray();
                size_t count = std::min(static_cast<size_t>(asks.Size()), static_cast<size_t>(10));
                for (size_t i = 0; i < count; ++i) {
                    if (asks[i].IsArray() && asks[i].GetArray().Size() >= 2) {
                        const auto& level = asks[i].GetArray();
                        snapshot.asks[i].price = std::stod(level[0].GetString());
                        snapshot.asks[i].quantity = std::stod(level[1].GetString());
                    }
                }
            }
        }
        
        return !snapshot.symbol.empty();
        
    } catch (const std::exception& e) {
        spdlog::warn("[MarketData] OrderBook parse error: {}", e.what());
        return false;
    }
}

void MarketDataProvider::publish_trade(const MarketTick& tick) {
    try {
        std::string json_data = serialize_trade(tick);
        
        // Create ZMQ message
        zmq::message_t topic_msg(tick.symbol.size());
        std::memcpy(topic_msg.data(), tick.symbol.c_str(), tick.symbol.size());
        
        zmq::message_t data_msg(json_data.size());
        std::memcpy(data_msg.data(), json_data.c_str(), json_data.size());
        
        // Publish to trades port (5558)
        if (trades_publisher_->send(topic_msg, zmq::send_flags::sndmore | zmq::send_flags::dontwait) &&
            trades_publisher_->send(data_msg, zmq::send_flags::dontwait)) {
            stats_.messages_published.fetch_add(1);
        }
        
    } catch (const std::exception& e) {
        spdlog::warn("[MarketData] Trade publish error: {}", e.what());
        stats_.errors.fetch_add(1);
    }
}

void MarketDataProvider::publish_orderbook(const OrderBookSnapshot& snapshot) {
    try {
        std::string json_data = serialize_orderbook(snapshot);
        
        // Create ZMQ message
        zmq::message_t topic_msg(snapshot.symbol.size());
        std::memcpy(topic_msg.data(), snapshot.symbol.c_str(), snapshot.symbol.size());
        
        zmq::message_t data_msg(json_data.size());
        std::memcpy(data_msg.data(), json_data.c_str(), json_data.size());
        
        // Publish to orderbook port (5559)
        if (orderbook_publisher_->send(topic_msg, zmq::send_flags::sndmore | zmq::send_flags::dontwait) &&
            orderbook_publisher_->send(data_msg, zmq::send_flags::dontwait)) {
            stats_.messages_published.fetch_add(1);
        }
        
    } catch (const std::exception& e) {
        spdlog::warn("[MarketData] OrderBook publish error: {}", e.what());
        stats_.errors.fetch_add(1);
    }
}

std::string MarketDataProvider::serialize_trade(const MarketTick& tick) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    
    writer.StartObject();
    writer.Key("timestamp_ns"); writer.Uint64(tick.timestamp_ns);
    writer.Key("symbol"); writer.String(tick.symbol.c_str());
    writer.Key("exchange"); writer.String(tick.exchange.c_str());
    writer.Key("price"); writer.Double(tick.price);
    writer.Key("quantity"); writer.Double(tick.quantity);
    writer.Key("side"); writer.String(tick.side.c_str());
    writer.Key("trade_id"); writer.String(tick.trade_id.c_str());
    writer.EndObject();
    
    return buffer.GetString();
}

std::string MarketDataProvider::serialize_orderbook(const OrderBookSnapshot& snapshot) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    
    writer.StartObject();
    writer.Key("timestamp_ns"); writer.Uint64(snapshot.timestamp_ns);
    writer.Key("symbol"); writer.String(snapshot.symbol.c_str());
    writer.Key("exchange"); writer.String(snapshot.exchange.c_str());
    writer.Key("sequence"); writer.Uint64(snapshot.sequence_number);
    
    // Bids
    writer.Key("bids");
    writer.StartArray();
    for (const auto& bid : snapshot.bids) {
        if (bid.price > 0 && bid.quantity > 0) {
            writer.StartArray();
            writer.Double(bid.price);
            writer.Double(bid.quantity);
            writer.EndArray();
        }
    }
    writer.EndArray();
    
    // Asks
    writer.Key("asks");
    writer.StartArray();
    for (const auto& ask : snapshot.asks) {
        if (ask.price > 0 && ask.quantity > 0) {
            writer.StartArray();
            writer.Double(ask.price);
            writer.Double(ask.quantity);
            writer.EndArray();
        }
    }
    writer.EndArray();
    
    writer.EndObject();
    
    return buffer.GetString();
}

uint64_t MarketDataProvider::get_timestamp_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

std::string MarketDataProvider::build_subscription_message() {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    
    if (exchange_ == "bybit") {
        // Bybit subscription format
        writer.StartObject();
        writer.Key("op"); writer.String("subscribe");
        writer.Key("args");
        writer.StartArray();
        
        for (const auto& symbol : symbols_) {
            // Subscribe to trades
            std::string trade_topic = "publicTrade." + symbol;
            writer.String(trade_topic.c_str());
            
            // Subscribe to orderbook
            std::string orderbook_topic = "orderbook.1." + symbol;
            writer.String(orderbook_topic.c_str());
        }
        
        writer.EndArray();
        writer.EndObject();
        
    } else if (exchange_ == "binance") {
        // Binance subscription format
        writer.StartObject();
        writer.Key("method"); writer.String("SUBSCRIBE");
        writer.Key("params");
        writer.StartArray();
        
        for (const auto& symbol : symbols_) {
            // Convert to lowercase for Binance
            std::string lower_symbol = symbol;
            std::transform(lower_symbol.begin(), lower_symbol.end(), lower_symbol.begin(), ::tolower);
            
            // Subscribe to trades
            std::string trade_stream = lower_symbol + "@trade";
            writer.String(trade_stream.c_str());
            
            // Subscribe to orderbook
            std::string orderbook_stream = lower_symbol + "@depth10@100ms";
            writer.String(orderbook_stream.c_str());
        }
        
        writer.EndArray();
        writer.Key("id"); writer.Int(1);
        writer.EndObject();
    }
    
    return buffer.GetString();
}

} // namespace latentspeed
