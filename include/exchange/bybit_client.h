/**
 * @file bybit_client.h
 * @brief Bybit exchange client implementation
 * @author jessiondiwangan@gmail.com
 * @date 2025
 * 
 * Direct Bybit API implementation with REST and WebSocket support
 */

#pragma once

#include "exchange_client.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <unordered_set>
#include <spdlog/spdlog.h>
#include <deque>

namespace latentspeed {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

/**
 * @class BybitClient
 * @brief Bybit exchange client implementation
 * 
 * Provides direct API access to Bybit exchange for order management
 * and real-time data streaming via WebSocket.
 */
class BybitClient : public ExchangeClient {
public:
    BybitClient();
    ~BybitClient() override;
    
    // ExchangeClient interface implementation
    bool initialize(const std::string& api_key, 
                   const std::string& api_secret,
                   bool testnet = false) override;
    
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;
    
    OrderResponse place_order(const OrderRequest& request) override;
    OrderResponse cancel_order(const std::string& client_order_id,
                              const std::optional<std::string>& symbol = std::nullopt,
                              const std::optional<std::string>& exchange_order_id = std::nullopt) override;
    OrderResponse modify_order(const std::string& client_order_id,
                              const std::optional<std::string>& new_quantity = std::nullopt,
                              const std::optional<std::string>& new_price = std::nullopt) override;
    OrderResponse query_order(const std::string& client_order_id) override;
    
    void set_order_update_callback(OrderUpdateCallback callback) override;
    void set_fill_callback(FillCallback callback) override;
    void set_error_callback(ErrorCallback callback) override;
    
    std::string get_exchange_name() const override { return "bybit"; }
    
    bool subscribe_to_orders(const std::vector<std::string>& symbols = {}) override;
    
private:
    // REST API methods
    std::string make_rest_request(const std::string& method,
                                 const std::string& endpoint,
                                 const std::string& params_json = "");
    
    std::string sign_request(const std::string& timestamp,
                            const std::string& api_key,
                            const std::string& recv_window,
                            const std::string& params) const;
    
    std::string hmac_sha256(const std::string& key, const std::string& data) const;
    
    // WebSocket methods
    void websocket_thread_func();
    void process_websocket_message(const std::string& message);
    bool send_websocket_auth();
    bool send_websocket_subscribe(const std::vector<std::string>& topics);
    void send_websocket_ping();
    void handle_order_update_message(const rapidjson::Document& doc);
    void handle_execution_message(const rapidjson::Document& doc);
    
    // Helper methods
    std::string get_timestamp_ms() const;
    OrderResponse parse_order_response(const std::string& json_response);
    std::string map_order_status(const std::string& bybit_status) const;
    
    struct RateLimiter {
        std::mutex mutex;
        std::deque<std::chrono::steady_clock::time_point> history;
        size_t max_per_window{8};
        std::chrono::milliseconds window{1000};

        void throttle();
    };

    bool ensure_rest_connection_locked();
    std::string perform_rest_request_locked(const std::string& method,
                                            const std::string& endpoint,
                                            const std::string& params_json);
    http::request<http::string_body> build_signed_request(const std::string& method,
                                                          const std::string& endpoint,
                                                          const std::string& params_json,
                                                          std::string& timestamp_out);
    void close_rest_connection_locked();
    void resync_pending_orders();
    void resync_order(const std::string& client_order_id, const OrderRequest& snapshot);
    void emit_order_snapshot(const rapidjson::Value& order_data);
    void emit_execution_snapshot(const rapidjson::Value& exec_data);

    // Connection state
    std::atomic<bool> connected_{false};
    std::atomic<bool> ws_connected_{false};
    std::atomic<bool> should_stop_{false};
    
    // REST API configuration
    std::string rest_host_;
    std::string rest_port_;
    std::string rest_target_;
    
    // WebSocket configuration
    std::string ws_host_;
    std::string ws_port_;
    std::string ws_target_;

    // Network components
    std::unique_ptr<net::io_context> ioc_;
    std::unique_ptr<ssl::context> ssl_ctx_;
    std::unique_ptr<net::io_context> rest_ioc_;
    std::unique_ptr<tcp::resolver> rest_resolver_;
    std::unique_ptr<websocket::stream<ssl::stream<tcp::socket>>> ws_;
    std::unique_ptr<ssl::stream<tcp::socket>> rest_stream_;
    std::vector<tcp::endpoint> rest_endpoints_;
    std::mutex rest_mutex_;
    bool rest_connected_{false};
    RateLimiter rest_rate_limiter_;
    std::chrono::steady_clock::time_point last_rest_connect_attempt_{};
    std::unique_ptr<std::thread> ws_thread_;

    // Message queue for WebSocket
    std::queue<std::string> ws_send_queue_;
    std::mutex ws_send_mutex_;
    std::condition_variable ws_send_cv_;
    
    // Order tracking
    std::map<std::string, OrderRequest> pending_orders_;
    std::mutex orders_mutex_;
    std::unordered_set<std::string> seen_exec_ids_;
    std::mutex seen_exec_mutex_;
    
    // Ping/Pong for WebSocket keepalive
    std::chrono::steady_clock::time_point last_ping_time_;
    std::chrono::steady_clock::time_point last_pong_time_;
    static constexpr int PING_INTERVAL_SEC = 20;
    static constexpr int PONG_TIMEOUT_SEC = 30;
};

} // namespace latentspeed
