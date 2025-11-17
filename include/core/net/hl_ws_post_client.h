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
#include <cstdint>
#include <functional>
#include <deque>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <rapidjson/document.h>
#include <spdlog/spdlog.h>

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
    uint64_t get_last_msg_ms() const { return last_msg_ms_.load(std::memory_order_acquire); }
    uint64_t get_last_ping_ms() const { return last_ping_ms_.load(std::memory_order_acquire); }

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
    static uint64_t now_ms();
    void mark_heartbeat_stale(uint64_t now_ms, uint64_t last_msg_ms, uint64_t last_ping_ms);
    void trace_rx(size_t bytes, const std::string& channel);
    void writer_loop();
    struct OutboundFrame;
    bool enqueue_frame(OutboundFrame frame);

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
    std::unique_ptr<std::thread> writer_thread_;

    std::mutex tx_mutex_;
    uint64_t next_id_{1};
    // Heartbeat thread to keep WS alive even when RX is idle
    std::unique_ptr<std::thread> hb_thread_;
    std::atomic<bool> stop_hb_{false};
    std::atomic<uint64_t> last_msg_ms_{0};
    std::atomic<uint64_t> last_ping_ms_{0};

    std::mutex corr_mutex_;
    struct Pending {
        std::string response; // filled when ready
        bool ready{false};
        std::condition_variable cv;
        std::mutex m;
        bool timed_out{false};
    };
    std::unordered_map<uint64_t, std::shared_ptr<Pending>> pending_;

    std::chrono::steady_clock::time_point last_rx_{};
    std::function<void(const std::string&, const rapidjson::Document&)> handler_;

    enum class FrameType { Post, Subscribe, Ping };
    struct OutboundFrame {
        FrameType type;
        uint64_t id{0};
        std::string payload;
        uint64_t meta_a{0};
        uint64_t meta_b{0};
        uint64_t meta_c{0};
        std::string tag; // optional diagnostics tag
    };
    std::atomic<bool> stop_writer_{false};
    std::mutex outbound_mutex_;
    std::condition_variable outbound_cv_;
    std::deque<OutboundFrame> outbound_queue_;

    // Diagnostics: keep a small rolling log of recent TX/RX events
    std::mutex diag_mutex_;
    std::deque<std::string> recent_tx_;
    std::deque<std::string> recent_rx_;
    void diag_push_tx(const std::string& s) {
        std::lock_guard<std::mutex> lk(diag_mutex_);
        if (recent_tx_.size() >= 16) recent_tx_.pop_front();
        recent_tx_.push_back(s);
    }
    void diag_push_rx(const std::string& s) {
        std::lock_guard<std::mutex> lk(diag_mutex_);
        if (recent_rx_.size() >= 16) recent_rx_.pop_front();
        recent_rx_.push_back(s);
    }
    void diag_dump_recent(const char* site) {
        try {
            spdlog::warn("[HL-WS][diag] dump at {} last_msg_ms={} last_ping_ms={}",
                         (site?site:"unknown"),
                         last_msg_ms_.load(std::memory_order_acquire),
                         last_ping_ms_.load(std::memory_order_acquire));
            std::lock_guard<std::mutex> lk(diag_mutex_);
            if (!recent_tx_.empty()) {
                for (const auto& s : recent_tx_) spdlog::warn("[HL-WS][diag] recent TX: {}", s);
            } else {
                spdlog::warn("[HL-WS][diag] recent TX: <none>");
            }
            if (!recent_rx_.empty()) {
                for (const auto& s : recent_rx_) spdlog::warn("[HL-WS][diag] recent RX: {}", s);
            } else {
                spdlog::warn("[HL-WS][diag] recent RX: <none>");
            }
        } catch (...) {}
    }

    // Schedule a one-shot diagnostic ping a few seconds after a post response
    void schedule_diag_ping_after_post(uint64_t post_id);
};

} // namespace latentspeed::netws
