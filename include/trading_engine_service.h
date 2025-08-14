#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <variant>
#include <cstdint>
#include <queue>
#include <mutex>
#include <condition_variable>

// ZeroMQ
#include <zmq.hpp>

// ccapi
#include "ccapi_cpp/ccapi_session.h"

// HTTP client for DEX gateway
#include <curl/curl.h>

namespace latentspeed {

// CEX order details
struct CexOrderDetails {
    std::string symbol;               // "ETH/USDT"
    std::string side;                 // "buy" or "sell"
    std::string order_type;           // "limit", "market", "stop", "stop_limit"
    std::string time_in_force;        // "gtc", "ioc", "fok", "post_only"
    double size;
    std::optional<double> price;
    std::optional<double> stop_price;
    bool reduce_only = false;
    std::optional<std::string> margin_mode;  // "cross", "isolated", or null
    std::map<std::string, std::string> params;
};

// AMM swap details
struct AmmSwapDetails {
    std::string chain;                // "ethereum"
    std::string protocol;             // "uniswap_v2", "sushiswap"
    std::string token_in;             // "ETH"
    std::string token_out;            // "USDC"
    std::string trade_mode;           // "exact_in" or "exact_out"
    std::optional<double> amount_in;
    std::optional<double> amount_out;
    int slippage_bps;                 // 50 bps = 0.50%
    int deadline_sec;                 // seconds from now
    std::string recipient;
    std::vector<std::string> route;   // optional hop list
    std::map<std::string, std::string> params;
};

// CLMM swap details
struct ClmmSwapDetails {
    std::string chain;
    std::string protocol;             // "uniswap_v3", "pancakeswap_v3"
    struct Pool {
        std::string token0;
        std::string token1;
        int fee_tier_bps;            // 500, 3000, 10000
    } pool;
    std::string trade_mode;
    std::optional<double> amount_in;
    std::optional<double> amount_out;
    int slippage_bps;
    std::optional<double> price_limit;
    int deadline_sec;
    std::string recipient;
    std::map<std::string, std::string> params;
};

// On-chain transfer details
struct TransferDetails {
    std::string chain;
    std::string token;               // "USDC" or native like "ETH"
    double amount;
    std::string to_address;
    std::map<std::string, std::string> params;
};

// Cancel details
struct CancelDetails {
    std::string symbol;
    std::string cl_id_to_cancel;
    std::optional<std::string> exchange_order_id;
};

// Replace details
struct ReplaceDetails {
    std::string symbol;
    std::string cl_id_to_replace;
    std::optional<double> new_price;
    std::optional<double> new_size;
};

// Union of all detail types
using OrderDetails = std::variant<CexOrderDetails, AmmSwapDetails, ClmmSwapDetails, TransferDetails, CancelDetails, ReplaceDetails>;

// Full ExecutionOrder per contract
struct ExecutionOrder {
    int version = 1;
    std::string cl_id;               // client order id (unique); idempotency key
    std::string action;              // "place", "cancel", "replace"
    std::string venue_type;          // "cex", "dex", "chain"
    std::string venue;               // "bybit", "binance", "uniswap_v3", etc.
    std::string product_type;        // "spot", "perpetual", "amm_swap", "clmm_swap", "transfer"
    OrderDetails details;
    uint64_t ts_ns;                  // send time (nanoseconds)
    std::map<std::string, std::string> tags;  // free-form metadata
};

// ExecutionReport
struct ExecutionReport {
    int version = 1;
    std::string cl_id;
    std::string status;              // "accepted", "rejected", "canceled", "replaced"
    std::optional<std::string> exchange_order_id;
    std::string reason_code;         // "ok", "invalid_params", "risk_blocked", etc.
    std::string reason_text;
    uint64_t ts_ns;
    std::map<std::string, std::string> tags;
};

// Fill
struct Fill {
    int version = 1;
    std::string cl_id;
    std::optional<std::string> exchange_order_id;
    std::string exec_id;             // unique per fill
    std::string symbol_or_pair;      // "ETH/USDT" or "ETH->USDC"
    double price;
    double size;
    std::string fee_currency;
    double fee_amount;
    std::optional<std::string> liquidity;  // "maker", "taker", or null
    uint64_t ts_ns;
    std::map<std::string, std::string> tags;
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
    // ZeroMQ communication (per contract)
    void zmq_order_receiver_thread();    // PULL from tcp://127.0.0.1:5601
    void zmq_publisher_thread();         // PUB to tcp://127.0.0.1:5602
    
    // Order processing
    void process_execution_order(const ExecutionOrder& order);
    void handle_cex_order(const ExecutionOrder& order, const CexOrderDetails& details);
    void handle_amm_swap(const ExecutionOrder& order, const AmmSwapDetails& details);
    void handle_clmm_swap(const ExecutionOrder& order, const ClmmSwapDetails& details);
    void handle_transfer(const ExecutionOrder& order, const TransferDetails& details);
    void handle_cancel(const ExecutionOrder& order, const CancelDetails& details);
    void handle_replace(const ExecutionOrder& order, const ReplaceDetails& details);
    
    // Message publishing
    void publish_execution_report(const ExecutionReport& report);
    void publish_fill(const Fill& fill);
    
    // DEX Gateway communication
    std::string call_gateway_api(const std::string& endpoint, const std::string& json_payload);
    
    // Market data
    void subscribe_market_data(const std::string& exchange, const std::string& instrument);
    
    // Message parsing and serialization
    ExecutionOrder parse_execution_order(const std::string& json_message);
    std::string serialize_execution_report(const ExecutionReport& report);
    std::string serialize_fill(const Fill& fill);
    
    // Utility functions
    uint64_t get_current_time_ns();
    std::string generate_exec_id();
    bool is_duplicate_order(const std::string& cl_id);
    void mark_order_processed(const std::string& cl_id);
    
    // ZeroMQ components (contract-compliant)
    std::unique_ptr<zmq::context_t> zmq_context_;
    std::unique_ptr<zmq::socket_t> order_receiver_socket_;  // PULL for orders
    std::unique_ptr<zmq::socket_t> report_publisher_socket_;  // PUB for reports/fills
    
    // ccapi components
    std::unique_ptr<ccapi::Session> ccapi_session_;
    std::unique_ptr<ccapi::SessionOptions> session_options_;
    std::unique_ptr<ccapi::SessionConfigs> session_configs_;
    
    // Threading
    std::unique_ptr<std::thread> order_receiver_thread_;
    std::unique_ptr<std::thread> publisher_thread_;
    std::atomic<bool> running_;
    
    // Configuration
    std::string order_endpoint_;         // tcp://127.0.0.1:5601
    std::string report_endpoint_;        // tcp://127.0.0.1:5602
    std::string gateway_base_url_;       // Hummingbot Gateway URL
    
    // Order tracking
    std::unordered_map<std::string, ccapi::Subscription> active_subscriptions_;
    std::unordered_map<std::string, ExecutionOrder> pending_orders_;  // cl_id -> order
    std::unordered_map<std::string, std::string> cex_order_mapping_;   // cl_id -> exchange_order_id
    std::unordered_set<std::string> processed_orders_;  // for idempotency
    
    // Thread-safe message queue for publishing
    std::queue<std::string> publish_queue_;
    std::mutex publish_mutex_;
    std::condition_variable publish_cv_;
};

} // namespace latentspeed
