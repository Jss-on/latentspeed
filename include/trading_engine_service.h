#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <map>

// ZeroMQ
#include <zmq.hpp>

// ccapi
#include "ccapi_cpp/ccapi_session.h"

namespace latentspeed {

struct OrderRequest {
    std::string exchange;
    std::string instrument;
    std::string side;  // "BUY" or "SELL"
    double quantity;
    double price;
    std::string order_type;  // "LIMIT", "MARKET"
    std::string correlation_id;
};

struct MarketDataSnapshot {
    std::string exchange;
    std::string instrument;
    double bid_price;
    double bid_size;
    double ask_price;
    double ask_size;
    std::string timestamp;
};

class TradingEngineService : public ccapi::EventHandler {
public:
    TradingEngineService();
    ~TradingEngineService();

    // Main service control
    bool initialize();
    void start();
    void stop();
    bool is_running() const { return running_; }

    // ccapi EventHandler implementation
    void processEvent(const ccapi::Event& event, ccapi::Session* sessionPtr) override;

private:
    // ZeroMQ communication
    void zmq_worker_thread();
    void handle_strategy_message(const std::string& message);
    void send_response(const std::string& response);

    // Order execution
    void execute_order(const OrderRequest& order);
    void subscribe_market_data(const std::string& exchange, const std::string& instrument);

    // Message parsing
    OrderRequest parse_order_request(const std::string& json_message);
    std::string create_response(const std::string& type, const std::string& data);

    // ZeroMQ components
    std::unique_ptr<zmq::context_t> zmq_context_;
    std::unique_ptr<zmq::socket_t> strategy_socket_;  // REP socket for strategy commands
    std::unique_ptr<zmq::socket_t> market_data_socket_;  // PUB socket for market data

    // ccapi components
    std::unique_ptr<ccapi::Session> ccapi_session_;
    std::unique_ptr<ccapi::SessionOptions> session_options_;
    std::unique_ptr<ccapi::SessionConfigs> session_configs_;

    // Threading
    std::unique_ptr<std::thread> zmq_thread_;
    std::atomic<bool> running_;

    // Configuration
    std::string strategy_endpoint_;
    std::string market_data_endpoint_;
    
    // Active subscriptions
    std::map<std::string, ccapi::Subscription> active_subscriptions_;
};

} // namespace latentspeed
