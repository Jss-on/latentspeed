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

    HyperliquidOrderBookDataSource();

    ~HyperliquidOrderBookDataSource() override;

    // ========================================================================
    // LIFECYCLE
    // ========================================================================

    bool initialize() override;

    void start() override;

    void stop() override;

    bool is_connected() const override;

    // ========================================================================
    // DATA RETRIEVAL (PULL MODEL)
    // ========================================================================

    std::optional<OrderBook> get_snapshot(const std::string& trading_pair) override;

    // ========================================================================
    // SUBSCRIPTION MANAGEMENT
    // ========================================================================

    void subscribe_orderbook(const std::string& trading_pair) override;

    void unsubscribe_orderbook(const std::string& trading_pair) override;

private:
    // ========================================================================
    // WEBSOCKET MANAGEMENT
    // ========================================================================

    void run_websocket();
    void connect_websocket();
    void read_messages();
    void process_message(const std::string& message);
    void process_orderbook_update(const nlohmann::json& data);
    void send_subscription(const std::string& coin);
    void send_unsubscription(const std::string& coin);
    void resubscribe_all();

    // ========================================================================
    // REST API
    // ========================================================================

    nlohmann::json rest_request(const nlohmann::json& request);
    void fetch_trading_pairs();

    // ========================================================================
    // UTILITIES
    // ========================================================================

    std::string normalize_symbol(const std::string& trading_pair) const;
    static uint64_t current_timestamp_ns();

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
