#pragma once

#include "connector/order_book_tracker_data_source.h"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace latentspeed::connector {

/**
 * @brief Hyperliquid-specific implementation of OrderBookTrackerDataSource
 * 
 * Connects to Hyperliquid WebSocket API for real-time market data:
 * - WebSocket URL: wss://api.hyperliquid.xyz/ws
 * - Channels: l2Book (order book snapshots/diffs)
 * - Symbol format: BTC, ETH, SOL (coin names without suffixes)
 */
class HyperliquidOrderBookDataSource : public OrderBookTrackerDataSource {
public:
    static constexpr const char* WS_URL = "api.hyperliquid.xyz";
    static constexpr const char* WS_PORT = "443";
    static constexpr const char* WS_PATH = "/ws";
    static constexpr const char* REST_URL = "https://api.hyperliquid.xyz/info";

    HyperliquidOrderBookDataSource() 
        : io_context_(),
          ssl_context_(ssl::context::tlsv12_client),
          ws_(nullptr),
          resolver_(io_context_),
          running_(false),
          connected_(false) {
        
        // Configure SSL context
        ssl_context_.set_default_verify_paths();
        ssl_context_.set_verify_mode(ssl::verify_peer);
    }

    ~HyperliquidOrderBookDataSource() override {
        stop();
    }

    // ========================================================================
    // LIFECYCLE
    // ========================================================================

    bool initialize() override {
        try {
            // Fetch trading pairs and metadata from REST API
            fetch_trading_pairs();
            return true;
        } catch (const std::exception& e) {
            spdlog::error("Failed to initialize HyperliquidOrderBookDataSource: {}", e.what());
            return false;
        }
    }

    void start() override {
        if (running_) return;
        
        running_ = true;
        ws_thread_ = std::thread([this]() { run_websocket(); });
        spdlog::info("HyperliquidOrderBookDataSource started");
    }

    void stop() override {
        if (!running_) return;
        
        running_ = false;
        
        // Close WebSocket
        if (ws_ && ws_->is_open()) {
            try {
                ws_->close(websocket::close_code::normal);
            } catch (...) {
                // Ignore errors on shutdown
            }
        }
        
        io_context_.stop();
        
        if (ws_thread_.joinable()) {
            ws_thread_.join();
        }
        
        connected_ = false;
        spdlog::info("HyperliquidOrderBookDataSource stopped");
    }

    bool is_connected() const override {
        return connected_;
    }

    // ========================================================================
    // DATA RETRIEVAL (PULL MODEL)
    // ========================================================================

    std::optional<OrderBook> get_snapshot(const std::string& trading_pair) override {
        try {
            // Convert trading pair to coin (e.g., "BTC-USD" -> "BTC")
            std::string coin = normalize_symbol(trading_pair);
            
            // Fetch snapshot via REST
            nlohmann::json request = {
                {"type", "l2Book"},
                {"coin", coin}
            };
            
            auto response = rest_request(request);
            
            if (response.contains("levels")) {
                OrderBook ob;
                ob.trading_pair = trading_pair;
                ob.timestamp = current_timestamp_ns();
                
                // Parse bids
                std::map<double, double> bids;
                for (const auto& level : response["levels"][0]) {
                    double price = std::stod(level["px"].get<std::string>());
                    double size = std::stod(level["sz"].get<std::string>());
                    bids[price] = size;
                }
                
                // Parse asks
                std::map<double, double> asks;
                for (const auto& level : response["levels"][1]) {
                    double price = std::stod(level["px"].get<std::string>());
                    double size = std::stod(level["sz"].get<std::string>());
                    asks[price] = size;
                }
                
                ob.apply_snapshot(bids, asks, 0);
                return ob;
            }
            
        } catch (const std::exception& e) {
            spdlog::error("Failed to get snapshot for {}: {}", trading_pair, e.what());
        }
        
        return std::nullopt;
    }

    // ========================================================================
    // SUBSCRIPTION MANAGEMENT
    // ========================================================================

    void subscribe_orderbook(const std::string& trading_pair) override {
        std::string coin = normalize_symbol(trading_pair);
        
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscribed_pairs_.insert(trading_pair);
        
        if (ws_ && ws_->is_open()) {
            send_subscription(coin);
        }
    }

    void unsubscribe_orderbook(const std::string& trading_pair) override {
        std::string coin = normalize_symbol(trading_pair);
        
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscribed_pairs_.erase(trading_pair);
        
        if (ws_ && ws_->is_open()) {
            send_unsubscription(coin);
        }
    }

private:
    // ========================================================================
    // WEBSOCKET MANAGEMENT
    // ========================================================================

    void run_websocket() {
        while (running_) {
            try {
                connect_websocket();
                resubscribe_all();
                read_messages();
                
            } catch (const std::exception& e) {
                spdlog::error("WebSocket error: {}", e.what());
                connected_ = false;
                
                if (running_) {
                    spdlog::info("Reconnecting in 5 seconds...");
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
            }
        }
    }

    void connect_websocket() {
        // Resolve hostname
        auto const results = resolver_.resolve(WS_URL, WS_PORT);
        
        // Create WebSocket stream with SSL
        ws_ = std::make_unique<websocket::stream<beast::ssl_stream<tcp::socket>>>(
            io_context_, ssl_context_
        );
        
        // Set SNI hostname
        if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), WS_URL)) {
            throw std::runtime_error("Failed to set SNI hostname");
        }
        
        // Connect
        auto ep = net::connect(ws_->next_layer().next_layer(), results);
        
        // SSL handshake
        ws_->next_layer().handshake(ssl::stream_base::client);
        
        // WebSocket handshake
        ws_->handshake(WS_URL, WS_PATH);
        
        connected_ = true;
        spdlog::info("Connected to Hyperliquid WebSocket");
    }

    void read_messages() {
        while (running_ && ws_ && ws_->is_open()) {
            beast::flat_buffer buffer;
            ws_->read(buffer);
            
            std::string message = beast::buffers_to_string(buffer.data());
            process_message(message);
        }
    }

    void process_message(const std::string& message) {
        try {
            auto json = nlohmann::json::parse(message);
            
            if (json.contains("channel") && json["channel"] == "l2Book") {
                process_orderbook_update(json["data"]);
            }
            
        } catch (const std::exception& e) {
            spdlog::error("Failed to process message: {}", e.what());
        }
    }

    void process_orderbook_update(const nlohmann::json& data) {
        if (!data.contains("coin")) return;
        
        std::string coin = data["coin"];
        std::string trading_pair = coin + "-USD";  // Reconstruct trading pair
        
        OrderBookMessage msg;
        msg.type = OrderBookMessage::Type::SNAPSHOT;
        msg.trading_pair = trading_pair;
        msg.timestamp = current_timestamp_ns();
        msg.data = data;  // Store the raw data
        
        emit_message(msg);
    }

    void send_subscription(const std::string& coin) {
        nlohmann::json sub = {
            {"method", "subscribe"},
            {"subscription", {
                {"type", "l2Book"},
                {"coin", coin}
            }}
        };
        
        ws_->write(net::buffer(sub.dump()));
        spdlog::info("Subscribed to l2Book for {}", coin);
    }

    void send_unsubscription(const std::string& coin) {
        nlohmann::json unsub = {
            {"method", "unsubscribe"},
            {"subscription", {
                {"type", "l2Book"},
                {"coin", coin}
            }}
        };
        
        ws_->write(net::buffer(unsub.dump()));
        spdlog::info("Unsubscribed from l2Book for {}", coin);
    }

    void resubscribe_all() {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        for (const auto& pair : subscribed_pairs_) {
            std::string coin = normalize_symbol(pair);
            send_subscription(coin);
        }
    }

    // ========================================================================
    // REST API
    // ========================================================================

    nlohmann::json rest_request(const nlohmann::json& request) {
        // Simple synchronous REST request using Beast HTTP
        net::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();
        
        beast::ssl_stream<tcp::socket> stream(ioc, ctx);
        
        // Resolve and connect
        tcp::resolver resolver(ioc);
        auto const results = resolver.resolve("api.hyperliquid.xyz", "443");
        net::connect(stream.next_layer(), results);
        
        // SSL handshake
        stream.handshake(ssl::stream_base::client);
        
        // Build POST request
        http::request<http::string_body> req{http::verb::post, "/info", 11};
        req.set(http::field::host, "api.hyperliquid.xyz");
        req.set(http::field::content_type, "application/json");
        req.body() = request.dump();
        req.prepare_payload();
        
        // Send request
        http::write(stream, req);
        
        // Read response
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);
        
        // Graceful close
        beast::error_code ec;
        stream.shutdown(ec);
        
        return nlohmann::json::parse(res.body());
    }

    void fetch_trading_pairs() {
        nlohmann::json request = {
            {"type", "meta"}
        };
        
        auto response = rest_request(request);
        
        if (response.contains("universe")) {
            for (const auto& asset : response["universe"]) {
                std::string name = asset["name"];
                trading_pairs_.push_back(name + "-USD");
            }
            spdlog::info("Fetched {} trading pairs", trading_pairs_.size());
        }
    }

    // ========================================================================
    // UTILITIES
    // ========================================================================

    std::string normalize_symbol(const std::string& trading_pair) const {
        // Convert "BTC-USD" -> "BTC"
        size_t pos = trading_pair.find('-');
        if (pos != std::string::npos) {
            return trading_pair.substr(0, pos);
        }
        return trading_pair;
    }

    static uint64_t current_timestamp_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    // ========================================================================
    // MEMBER VARIABLES
    // ========================================================================

    net::io_context io_context_;
    ssl::context ssl_context_;
    std::unique_ptr<websocket::stream<beast::ssl_stream<tcp::socket>>> ws_;
    tcp::resolver resolver_;
    
    std::thread ws_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> connected_;
    
    std::mutex subscriptions_mutex_;
    std::unordered_set<std::string> subscribed_pairs_;
    std::vector<std::string> trading_pairs_;
};

} // namespace latentspeed::connector
