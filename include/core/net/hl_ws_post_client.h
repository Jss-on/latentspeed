/**
 * @file hl_ws_post_client.h
 * @brief Minimal Hyperliquid WebSocket "post" client with id correlation and heartbeat.
 */

#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <optional>
#include <chrono>
#include <functional>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <rapidjson/document.h>

namespace latentspeed::netws {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

class HlWsPostClient {
public:
    HlWsPostClient();
    ~HlWsPostClient();

    // Connect to ws url: e.g. wss://api.hyperliquid.xyz/ws
    bool connect(const std::string& ws_url);
    void close();
    bool is_connected() const { return connected_.load(std::memory_order_acquire); }

    // Send a "post" with correlation id and wait for the response payload.
    // type: "info" or "action"
    // payload_json: raw JSON string for the inner payload object
    // Returns response payload object as a raw JSON string, or nullopt on timeout/error.
    std::optional<std::string> post(const std::string& type,
                                    const std::string& payload_json,
                                    std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));

    // Subscribe helper: builds {"method":"subscribe","subscription":{...}} and sends it.
    bool subscribe(const std::string& type,
                   const std::vector<std::pair<std::string,std::string>>& kv_fields);
    // Subscribe with boolean field support (e.g., aggregateByTime)
    bool subscribe_with_bool(const std::string& type,
                             const std::vector<std::pair<std::string,std::string>>& kv_fields,
                             const std::vector<std::pair<std::string,bool>>& bool_fields);

    // Set message handler for non-post channels (e.g., orderUpdates, userEvents, userFills)
    void set_message_handler(std::function<void(const std::string& channel, const rapidjson::Document& doc)> handler) {
        handler_ = std::move(handler);
    }

private:
    // RX thread
    void rx_loop();
    void send_ping();
    bool ensure_connected_locked();
    static void parse_ws_url(const std::string& url, std::string& host, std::string& port, std::string& target, bool& tls);

    std::atomic<bool> connected_{false};
    std::atomic<bool> stop_{false};
    std::unique_ptr<net::io_context> ioc_;
    std::unique_ptr<ssl::context> ssl_ctx_;
    std::unique_ptr<websocket::stream<ssl::stream<tcp::socket>>> wss_;
    std::unique_ptr<websocket::stream<tcp::socket>> ws_;
    std::unique_ptr<tcp::resolver> resolver_;
    bool use_tls_{true};
    std::string host_;
    std::string port_;
    std::string target_;
    std::unique_ptr<std::thread> rx_thread_;

    std::mutex tx_mutex_;
    uint64_t next_id_{1};
    // Heartbeat thread to keep WS alive even when RX is idle
    std::unique_ptr<std::thread> hb_thread_;
    std::atomic<bool> stop_hb_{false};

    std::mutex corr_mutex_;
    struct Pending {
        std::string response; // filled when ready
        bool ready{false};
        std::condition_variable cv;
    };
    std::unordered_map<uint64_t, std::shared_ptr<Pending>> pending_;

    std::chrono::steady_clock::time_point last_rx_{};
    std::function<void(const std::string&, const rapidjson::Document&)> handler_;
};

} // namespace latentspeed::netws
