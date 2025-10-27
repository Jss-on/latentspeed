#pragma once

#include "connector/user_stream_tracker_data_source.h"
#include "connector/exchange/hyperliquid/hyperliquid_auth.h"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace latentspeed::connector {

using hyperliquid::HyperliquidAuth;

/**
 * @brief Hyperliquid-specific implementation of UserStreamTrackerDataSource
 * 
 * Connects to Hyperliquid WebSocket API for authenticated user-specific data:
 * - Channels: user (includes orders, fills, funding)
 * - Authentication: Uses wallet address for subscription
 * - Real-time updates for order status, fills, and account state
 */
class HyperliquidUserStreamDataSource : public UserStreamTrackerDataSource {
public:
    static constexpr const char* WS_URL = "api.hyperliquid.xyz";
    static constexpr const char* WS_PORT = "443";
    static constexpr const char* WS_PATH = "/ws";

    HyperliquidUserStreamDataSource(std::shared_ptr<HyperliquidAuth> auth);

    ~HyperliquidUserStreamDataSource() override;

    // ========================================================================
    // LIFECYCLE
    // ========================================================================

    bool initialize() override;

    void start() override;

    void stop() override;

    bool is_connected() const override;

    // ========================================================================
    // SUBSCRIPTION MANAGEMENT
    // ========================================================================

    void subscribe_to_order_updates() override;

    void subscribe_to_balance_updates() override;

    void subscribe_to_position_updates() override;

private:
    // ========================================================================
    // WEBSOCKET MANAGEMENT
    // ========================================================================

    // Private method declarations - implementations in .cpp file
    void run_websocket();
    void connect_websocket();
    void read_messages();
    void process_message(const std::string& message);
    void process_user_update(const nlohmann::json& data);
    void process_fill(const nlohmann::json& fill);
    void process_order_update(const nlohmann::json& order);
    void process_funding_update(const nlohmann::json& funding);
    void process_ledger_update(const nlohmann::json& update);
    void process_liquidation(const nlohmann::json& liquidation);
    void send_user_subscription();
    static uint64_t current_timestamp_ns();

    // ========================================================================
    // MEMBER VARIABLES
    // ========================================================================

    std::shared_ptr<HyperliquidAuth> auth_;
    
    net::io_context io_context_;
    ssl::context ssl_context_;
    std::unique_ptr<websocket::stream<beast::ssl_stream<tcp::socket>>> ws_;
    tcp::resolver resolver_;
    
    std::thread ws_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> connected_;
    
    bool subscribed_to_orders_ = false;
    bool subscribed_to_balances_ = false;
    bool subscribed_to_positions_ = false;
};

} // namespace latentspeed::connector
