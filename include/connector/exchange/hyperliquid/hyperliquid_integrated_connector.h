#pragma once

#include "connector/connector_base.h"
#include "connector/client_order_tracker.h"
#include "connector/exchange/hyperliquid/hyperliquid_auth.h"
#include "connector/exchange/hyperliquid/hyperliquid_web_utils.h"
#include "connector/exchange/hyperliquid/hyperliquid_user_stream_data_source.h"
#include "connector/exchange/hyperliquid/hyperliquid_marketstream_adapter.h"
#include "connector/zmq_order_event_publisher.h"
#include "exchange_interface.h"  // Your existing HyperliquidExchange

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <future>

namespace latentspeed {

namespace net = boost::asio;

/**
 * @brief Integrated Hyperliquid connector that combines:
 *        - Your existing marketstream (HyperliquidExchange) for market data
 *        - Phase 5 user stream for authenticated order updates
 *        - ZMQ publishing for order events
 *        - Phase 2 order tracking
 *        - Non-blocking order placement
 */
class HyperliquidIntegratedConnector : public ConnectorBase {
public:
    /**
     * @brief Constructor
     * @param auth Hyperliquid authentication
     * @param existing_exchange Your existing marketstream exchange (reused!)
     * @param zmq_context Your existing ZMQ context (reused!)
     * @param zmq_endpoint ZMQ endpoint for order events (e.g., "tcp://*:5556")
     * @param testnet Whether to use testnet
     */
    HyperliquidIntegratedConnector(
        std::shared_ptr<HyperliquidAuth> auth,
        std::shared_ptr<HyperliquidExchange> existing_exchange,
        std::shared_ptr<zmq::context_t> zmq_context,
        const std::string& zmq_endpoint,
        bool testnet = false
    );

    ~HyperliquidIntegratedConnector() override;

    // Lifecycle
    
    bool initialize() override;

    void start() override;

    void stop() override;

    // Order placement (Phase 5 non-blocking pattern)
    
    std::string buy(const OrderParams& params) override;
    std::string sell(const OrderParams& params) override;
    std::future<bool> cancel(const std::string& trading_pair, const std::string& client_order_id) override;

    // Query methods
    
    std::vector<InFlightOrder> get_open_orders(const std::string& trading_pair = "") const override;
    std::optional<InFlightOrder> get_order(const std::string& client_order_id) const override;

    // Connector info
    
    std::string connector_name() const override;
    bool is_connected() const override;

    // Access to components (for advanced usage)
    
    std::shared_ptr<HyperliquidExchange> get_marketstream_exchange() const;
    std::shared_ptr<ZMQOrderEventPublisher> get_zmq_publisher() const;

private:
    // Private helper methods - implementations in .cpp
    std::string place_order(const OrderParams& params, TradeType trade_type);
    void place_order_and_process_update(const std::string& client_order_id);
    std::string execute_place_order(const InFlightOrder& order);
    bool execute_cancel_order(const std::string& trading_pair, const std::string& client_order_id);
    void setup_user_stream_callbacks();
    void handle_user_stream_message(const UserStreamMessage& msg);
    void process_order_update(const UserStreamMessage& msg);
    void process_trade_update(const UserStreamMessage& msg);
    void setup_order_tracker_callbacks();
    bool fetch_trading_rules();
    bool validate_order_params(const OrderParams& params) const;
    void emit_order_failure_event(const std::string& client_order_id, const std::string& reason);

    // Members
    std::shared_ptr<HyperliquidAuth> auth_;
    std::shared_ptr<HyperliquidExchange> existing_exchange_;
    bool testnet_;
    bool running_ = false;
    
    // Async execution
    net::io_context io_context_;
    net::executor_work_guard<net::io_context::executor_type> work_guard_;
    std::thread async_thread_;
    
    // Components
    std::shared_ptr<HyperliquidMarketstreamAdapter> marketstream_adapter_;
    std::shared_ptr<HyperliquidUserStreamDataSource> user_stream_;
    std::shared_ptr<ZMQOrderEventPublisher> zmq_publisher_;
    std::shared_ptr<ClientOrderTracker> order_tracker_;
};

}  // namespace latentspeed
