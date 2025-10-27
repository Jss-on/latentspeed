#pragma once

#include "connector/connector_base.h"
#include "connector/client_order_tracker.h"
#include "connector/exchange/hyperliquid/hyperliquid_auth.h"
#include "connector/exchange/hyperliquid/hyperliquid_web_utils.h"
#include "connector/exchange/hyperliquid/hyperliquid_order_book_data_source.h"
#include "connector/exchange/hyperliquid/hyperliquid_user_stream_data_source.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <memory>
#include <future>
#include <functional>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace latentspeed::connector {

// Import types from sub-namespace
using hyperliquid::HyperliquidAuth;
using hyperliquid::HyperliquidWebUtils;

/**
 * @brief Hyperliquid Perpetual Futures Connector
 * 
 * Implements the Hummingbot event-driven order lifecycle pattern for Hyperliquid:
 * 1. buy()/sell() returns immediately with client_order_id
 * 2. Order is tracked BEFORE API call
 * 3. Async execution submits order to exchange
 * 4. WebSocket user stream provides real-time updates
 * 5. Events emitted on state changes
 */
class HyperliquidPerpetualConnector : public ConnectorBase {
public:
    static constexpr const char* REST_URL = "https://api.hyperliquid.xyz";
    static constexpr const char* CREATE_ORDER_URL = "/exchange";
    static constexpr const char* CANCEL_ORDER_URL = "/exchange";
    static constexpr const char* INFO_URL = "/info";

    HyperliquidPerpetualConnector(
        std::shared_ptr<HyperliquidAuth> auth,
        bool testnet = false
    );

    ~HyperliquidPerpetualConnector() override;

    // ========================================================================
    // LIFECYCLE
    // ========================================================================

    bool initialize();

    void start();

    void stop();

    bool is_connected() const;

    // ========================================================================
    // ORDER PLACEMENT (Hummingbot Pattern)
    // ========================================================================

    /**
     * @brief Place a BUY order (non-blocking)
     * @return client_order_id immediately
     */
    std::string buy(const OrderParams& params);

    /**
     * @brief Place a SELL order (non-blocking)
     * @return client_order_id immediately
     */
    std::string sell(const OrderParams& params);

    /**
     * @brief Cancel an order
     * @return future that resolves when cancellation is processed
     */
    std::future<bool> cancel(const std::string& trading_pair, 
                            const std::string& client_order_id);

    // ========================================================================
    // ORDER TRACKING ACCESS
    // ========================================================================

    std::optional<InFlightOrder> get_order(const std::string& client_order_id) const;

    std::vector<InFlightOrder> get_open_orders() const;

    // ========================================================================
    // EVENT LISTENER
    // ========================================================================

    void set_event_listener(std::shared_ptr<OrderEventListener> listener);

    // ========================================================================
    // CONNECTORBASE PURE VIRTUAL IMPLEMENTATIONS
    // ========================================================================

    std::string name() const override;

    std::string domain() const override;

    ConnectorType connector_type() const override;

    bool connect() override;

    void disconnect() override;

    bool is_ready() const override;

    bool cancel(const std::string& client_order_id) override;

    // Legacy method name support
    std::string get_connector_name() const;

    std::optional<TradingRule> get_trading_rule(const std::string& trading_pair) const override;

    std::vector<TradingRule> get_all_trading_rules() const override;

    uint64_t current_timestamp_ns() const override;

private:
    // ========================================================================
    // ORDER PLACEMENT IMPLEMENTATION
    // ========================================================================

    std::string place_order(const OrderParams& params, TradeType trade_type);
    void place_order_and_process_update(const std::string& client_order_id);
    std::pair<std::string, uint64_t> execute_place_order(const InFlightOrder& order);
    bool execute_cancel(const std::string& trading_pair, const std::string& client_order_id);
    bool execute_cancel_order(const InFlightOrder& order);

    // ========================================================================
    // USER STREAM PROCESSING
    // ========================================================================

    void handle_user_stream_message(const UserStreamMessage& msg);
    void process_trade_update(const UserStreamMessage& msg);
    void process_order_update(const UserStreamMessage& msg);

    // ========================================================================
    // REST API HELPERS
    // ========================================================================

    nlohmann::json api_post_with_auth(const std::string& endpoint, const nlohmann::json& action);
    nlohmann::json rest_post(const std::string& endpoint, const nlohmann::json& data);
    void fetch_trading_rules();

    // ========================================================================
    // VALIDATION & UTILITIES
    // ========================================================================

    bool validate_order_params(const OrderParams& params) const;
    std::string extract_coin_from_pair(const std::string& trading_pair) const;
    void emit_order_created_event(const std::string& client_order_id, const std::string& exchange_order_id);
    void emit_order_failure_event(const std::string& client_order_id, const std::string& reason);


    // ========================================================================
    // MEMBER VARIABLES
    // ========================================================================

    std::shared_ptr<HyperliquidAuth> auth_;
    bool testnet_;
    
    ClientOrderTracker order_tracker_;
    
    std::shared_ptr<HyperliquidOrderBookDataSource> orderbook_data_source_;
    std::shared_ptr<HyperliquidUserStreamDataSource> user_stream_data_source_;
    
    std::shared_ptr<OrderEventListener> event_listener_;
    
    // Async execution
    net::io_context io_context_;
    net::executor_work_guard<net::io_context::executor_type> work_guard_;
    std::thread async_thread_;
    std::atomic<bool> running_;
    
    // Trading rules and metadata
    mutable std::mutex trading_rules_mutex_;
    std::unordered_map<std::string, TradingRule> trading_rules_;
    std::unordered_map<std::string, int> coin_to_asset_;  // coin -> asset_index
};

} // namespace latentspeed::connector
