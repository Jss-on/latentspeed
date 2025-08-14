/**
 * @file trading_engine_service.h
 * @brief High-performance trading engine service for multi-venue order execution
 * @author jessiondiwangan@gmail.com
 * @date 2024
 * 
 * This header file defines the core trading engine service that provides:
 * - Multi-venue order execution (CEX, DEX, on-chain)
 * - Real-time market data processing
 * - ZeroMQ-based communication protocol
 * - Backtest simulation capabilities
 * - Risk management and order lifecycle management
 */

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

/**
 * @namespace latentspeed
 * @brief Main namespace for Latentspeed trading infrastructure
 */
namespace latentspeed {

/**
 * @struct CexOrderDetails
 * @brief Centralized exchange order specification
 * 
 * Contains all parameters required to place an order on centralized exchanges
 * like Binance, Bybit, OKX, etc. Supports spot, futures, and options trading.
 */
struct CexOrderDetails {
    std::string symbol;               ///< Trading symbol (e.g., "ETH/USDT")
    std::string side;                 ///< Order side: "buy" or "sell"
    std::string order_type;           ///< Order type: "limit", "market", "stop", "stop_limit"
    std::string time_in_force;        ///< Time in force: "gtc", "ioc", "fok", "post_only"
    double size;                      ///< Order quantity/size
    std::optional<double> price;      ///< Limit price (required for limit orders)
    std::optional<double> stop_price; ///< Stop trigger price (for stop orders)
    bool reduce_only = false;         ///< Whether this order can only reduce position
    std::optional<std::string> margin_mode;  ///< Margin mode: "cross", "isolated", or null
    std::map<std::string, std::string> params; ///< Exchange-specific parameters
};

/**
 * @struct AmmSwapDetails
 * @brief Automated Market Maker swap specification
 * 
 * Defines parameters for token swaps on AMM protocols like Uniswap V2,
 * SushiSwap, PancakeSwap V2, etc. Supports both exact input and exact output swaps.
 */
struct AmmSwapDetails {
    std::string chain;                ///< Blockchain network (e.g., "ethereum", "bsc")
    std::string protocol;             ///< AMM protocol (e.g., "uniswap_v2", "sushiswap")
    std::string token_in;             ///< Input token symbol (e.g., "ETH")
    std::string token_out;            ///< Output token symbol (e.g., "USDC")
    std::string trade_mode;           ///< Swap mode: "exact_in" or "exact_out"
    std::optional<double> amount_in;  ///< Input amount (for exact_in trades)
    std::optional<double> amount_out; ///< Output amount (for exact_out trades)
    int slippage_bps;                 ///< Maximum slippage in basis points (e.g., 50 = 0.50%)
    int deadline_sec;                 ///< Transaction deadline in seconds from now
    std::string recipient;            ///< Token recipient address
    std::vector<std::string> route;   ///< Optional routing path for multi-hop swaps
    std::map<std::string, std::string> params; ///< Protocol-specific parameters
};

/**
 * @struct ClmmSwapDetails
 * @brief Concentrated Liquidity Market Maker swap specification
 * 
 * Defines parameters for token swaps on CLMM protocols like Uniswap V3,
 * PancakeSwap V3, etc. Supports concentrated liquidity and price range specifications.
 */
struct ClmmSwapDetails {
    std::string chain;                ///< Blockchain network
    std::string protocol;             ///< CLMM protocol (e.g., "uniswap_v3", "pancakeswap_v3")
    
    /**
     * @struct Pool
     * @brief CLMM pool specification
     */
    struct Pool {
        std::string token0;           ///< First token in the pool
        std::string token1;           ///< Second token in the pool  
        int fee_tier_bps;            ///< Pool fee tier in basis points (500, 3000, 10000)
    } pool;
    
    std::string trade_mode;           ///< Swap mode: "exact_in" or "exact_out"
    std::optional<double> amount_in;  ///< Input amount (for exact_in trades)
    std::optional<double> amount_out; ///< Output amount (for exact_out trades)
    int slippage_bps;                 ///< Maximum slippage in basis points
    std::optional<double> price_limit; ///< Price limit for the swap
    int deadline_sec;                 ///< Transaction deadline in seconds
    std::string recipient;            ///< Token recipient address
    std::map<std::string, std::string> params; ///< Protocol-specific parameters
};

/**
 * @struct TransferDetails
 * @brief On-chain token transfer specification
 * 
 * Defines parameters for direct token transfers on blockchain networks.
 * Supports both native tokens (ETH, BNB) and ERC-20/BEP-20 tokens.
 */
struct TransferDetails {
    std::string chain;                ///< Blockchain network
    std::string token;               ///< Token symbol ("USDC") or native token ("ETH")
    double amount;                   ///< Transfer amount
    std::string to_address;          ///< Recipient wallet address
    std::map<std::string, std::string> params; ///< Chain-specific parameters
};

/**
 * @struct CancelDetails
 * @brief Order cancellation specification
 * 
 * Contains information needed to cancel an existing order.
 */
struct CancelDetails {
    std::string symbol;               ///< Trading symbol of the order to cancel
    std::string cl_id_to_cancel;     ///< Client order ID to cancel
    std::optional<std::string> exchange_order_id; ///< Exchange order ID (if available)
};

/**
 * @struct ReplaceDetails
 * @brief Order replacement/modification specification
 * 
 * Contains parameters for modifying an existing order (price and/or size).
 */
struct ReplaceDetails {
    std::string symbol;               ///< Trading symbol of the order to replace
    std::string cl_id_to_replace;    ///< Client order ID to replace
    std::optional<double> new_price; ///< New price (if changing)
    std::optional<double> new_size;  ///< New size (if changing)
};

/**
 * @typedef OrderDetails
 * @brief Variant type containing all possible order detail types
 * 
 * This union type allows a single ExecutionOrder to represent any type of
 * trading operation supported by the system.
 */
using OrderDetails = std::variant<CexOrderDetails, AmmSwapDetails, ClmmSwapDetails, TransferDetails, CancelDetails, ReplaceDetails>;

/**
 * @struct ExecutionOrder
 * @brief Complete order specification for the trading engine
 * 
 * This is the main order structure that gets sent to the trading engine.
 * It contains all necessary information to execute any supported trading operation.
 */
struct ExecutionOrder {
    int version = 1;                 ///< Protocol version for compatibility
    std::string cl_id;               ///< Client order ID (unique); idempotency key
    std::string action;              ///< Order action: "place", "cancel", "replace"
    std::string venue_type;          ///< Venue category: "cex", "dex", "chain"
    std::string venue;               ///< Specific venue: "bybit", "binance", "uniswap_v3", etc.
    std::string product_type;        ///< Product type: "spot", "perpetual", "amm_swap", "clmm_swap", "transfer"
    OrderDetails details;            ///< Order-specific parameters (variant type)
    uint64_t ts_ns;                  ///< Order timestamp in nanoseconds
    std::map<std::string, std::string> tags;  ///< Free-form metadata and routing tags
};

/**
 * @struct ExecutionReport
 * @brief Order execution status report
 * 
 * Generated by the trading engine to report the status of order processing.
 * Sent back to the strategy/client that submitted the original order.
 */
struct ExecutionReport {
    int version = 1;                 ///< Protocol version
    std::string cl_id;               ///< Client order ID from original order
    std::string status;              ///< Execution status: "accepted", "rejected", "canceled", "replaced"
    std::optional<std::string> exchange_order_id; ///< Exchange-assigned order ID
    std::string reason_code;         ///< Status code: "ok", "invalid_params", "risk_blocked", etc.
    std::string reason_text;         ///< Human-readable status description
    uint64_t ts_ns;                  ///< Report timestamp in nanoseconds
    std::map<std::string, std::string> tags;  ///< Metadata and routing information
};

/**
 * @struct Fill
 * @brief Trade execution fill report
 * 
 * Generated when an order gets filled (partially or completely).
 * Contains detailed execution information including price, size, and fees.
 */
struct Fill {
    int version = 1;                 ///< Protocol version
    std::string cl_id;               ///< Client order ID from original order
    std::optional<std::string> exchange_order_id; ///< Exchange-assigned order ID
    std::string exec_id;             ///< Unique execution ID for this fill
    std::string symbol_or_pair;      ///< Trading pair: "ETH/USDT" or "ETH->USDC"
    double price;                    ///< Execution price
    double size;                     ///< Filled quantity
    std::string fee_currency;        ///< Currency in which fees are charged
    double fee_amount;               ///< Fee amount paid
    std::optional<std::string> liquidity; ///< Liquidity type: "maker", "taker", or null
    uint64_t ts_ns;                  ///< Fill timestamp in nanoseconds
    std::map<std::string, std::string> tags;  ///< Metadata and routing information
};

/**
 * @struct PreprocessedTrade
 * @brief Preprocessed trade data from market data pipeline
 * 
 * Contains trade information that has been enriched with preprocessing
 * features like volatility, transaction price analysis, etc.
 */
struct PreprocessedTrade {
    // Common metadata
    int schema_version = 1;          ///< Schema version for compatibility
    std::string partition_id;        ///< Data partition identifier
    uint64_t seq;                    ///< Sequence number
    uint64_t receipt_timestamp_ns;   ///< Receipt timestamp in nanoseconds
    std::string preprocessing_timestamp; ///< Preprocessing completion timestamp
    std::string symbol_norm;         ///< Normalized symbol name
    
    // Original cryptofeed fields
    std::string exchange;            ///< Exchange identifier
    std::string symbol;              ///< Original exchange symbol
    double price;                    ///< Trade execution price
    double amount;                   ///< Trade size/volume
    std::string timestamp;           ///< Trade timestamp
    std::string side;                ///< Trade side: "buy" or "sell"
    
    // Preprocessing fields
    double transaction_price;        ///< Processed transaction price
    double trading_volume;           ///< Aggregated trading volume
    double volatility_transaction_price; ///< Price volatility metric
    int window_size;                 ///< Analysis window size
};

/**
 * @struct PreprocessedOrderbook
 * @brief Preprocessed order book data from market data pipeline
 * 
 * Contains order book information enriched with preprocessing features
 * like spread analysis, depth metrics, order flow imbalance, etc.
 */
struct PreprocessedOrderbook {
    // Common metadata
    int schema_version = 1;          ///< Schema version for compatibility
    std::string partition_id;        ///< Data partition identifier
    uint64_t seq;                    ///< Sequence number
    uint64_t receipt_timestamp_ns;   ///< Receipt timestamp in nanoseconds
    std::string preprocessing_timestamp; ///< Preprocessing completion timestamp
    std::string symbol_norm;         ///< Normalized symbol name
    
    // Original cryptofeed fields
    std::string exchange;            ///< Exchange identifier
    std::string symbol;              ///< Original exchange symbol
    std::string timestamp;           ///< Order book timestamp
    // book data would be here but we focus on preprocessed features
    
    // Preprocessing fields
    double best_bid_price;           ///< Best bid price
    double best_bid_size;            ///< Best bid size
    double best_ask_price;           ///< Best ask price
    double best_ask_size;            ///< Best ask size
    double midpoint;                 ///< Mid price (bid+ask)/2
    double relative_spread;          ///< Relative bid-ask spread
    double breadth;                  ///< Order book breadth metric
    double imbalance_lvl1;          ///< Level 1 order flow imbalance
    double bid_depth_n;             ///< Bid-side depth (N levels)
    double ask_depth_n;             ///< Ask-side depth (N levels)
    double depth_n;                 ///< Total depth (N levels)
    double volatility_mid;          ///< Mid-price volatility
    double ofi_rolling;             ///< Rolling order flow imbalance
    int window_size;                ///< Analysis window size
};

/**
 * @struct MarketDataSnapshot
 * @brief Real-time market data snapshot
 * 
 * Basic market data structure used for live trading and backtesting.
 */
struct MarketDataSnapshot {
    std::string exchange;            ///< Exchange identifier
    std::string instrument;          ///< Trading instrument/symbol
    double bid_price;                ///< Current best bid price
    double bid_size;                 ///< Current best bid size
    double ask_price;                ///< Current best ask price
    double ask_size;                 ///< Current best ask size
    std::string timestamp;           ///< Snapshot timestamp
};

/**
 * @class TradingEngineService
 * @brief High-performance multi-venue trading engine
 * 
 * The core trading engine service that handles:
 * - Order execution across multiple venues (CEX, DEX, on-chain)
 * - Real-time market data processing and distribution
 * - ZeroMQ-based communication with trading strategies
 * - Risk management and order lifecycle management
 * - Backtest simulation capabilities
 * 
 * This class implements the ccapi::EventHandler interface to receive
 * market data and execution updates from supported exchanges.
 */
class TradingEngineService : public ccapi::EventHandler {
public:
    /**
     * @brief Default constructor
     * 
     * Initializes the trading engine with default configuration.
     * Sets up ZeroMQ endpoints, gateway URLs, and backtest parameters.
     */
    TradingEngineService();
    
    /**
     * @brief Destructor
     * 
     * Ensures clean shutdown of all threads and releases resources.
     */
    ~TradingEngineService();

    /**
     * @brief Initialize the trading engine
     * @return true if initialization successful, false otherwise
     * 
     * Sets up ZeroMQ sockets, ccapi session, and prepares all components
     * for operation. Must be called before start().
     */
    bool initialize();
    
    /**
     * @brief Start the trading engine service
     * 
     * Launches all worker threads for order processing, market data
     * consumption, and message publishing. Non-blocking call.
     */
    void start();
    
    /**
     * @brief Stop the trading engine service
     * 
     * Gracefully shuts down all worker threads and closes connections.
     * Waits for all threads to complete before returning.
     */
    void stop();
    
    /**
     * @brief Check if the service is currently running
     * @return true if service is active, false otherwise
     */
    bool is_running() const { return running_; }

    /**
     * @brief Process events from ccapi (market data, execution updates)
     * @param event The ccapi event to process
     * @param sessionPtr Pointer to the ccapi session
     * 
     * Implementation of ccapi::EventHandler interface. Handles market data
     * updates and execution confirmations from connected exchanges.
     */
    void processEvent(const ccapi::Event& event, ccapi::Session* sessionPtr) override;

private:
    /// @name ZeroMQ Communication Threads
    /// @{
    /**
     * @brief Order receiver thread (PULL socket)
     * 
     * Receives ExecutionOrder messages from strategies on tcp://127.0.0.1:5601
     * and processes them for execution.
     */
    void zmq_order_receiver_thread();
    
    /**
     * @brief Message publisher thread (PUB socket)
     * 
     * Publishes ExecutionReport and Fill messages to strategies on tcp://127.0.0.1:5602
     */
    void zmq_publisher_thread();
    /// @}
    
    /// @name Market Data Subscription Threads
    /// @{
    /**
     * @brief Preprocessed trade data subscriber thread
     * 
     * Subscribes to preprocessed trade data on tcp://{host}:5556
     */
    void zmq_trade_subscriber_thread();
    
    /**
     * @brief Preprocessed order book subscriber thread
     * 
     * Subscribes to preprocessed order book data on tcp://{host}:5557
     */
    void zmq_orderbook_subscriber_thread();
    /// @}
    
    /// @name Order Processing Methods
    /// @{
    /**
     * @brief Process incoming execution order
     * @param order The execution order to process
     * 
     * Main order processing dispatcher that routes orders to appropriate handlers
     * based on venue type and product type.
     */
    void process_execution_order(const ExecutionOrder& order);
    
    /**
     * @brief Handle centralized exchange orders
     * @param order The execution order
     * @param details CEX-specific order details
     */
    void handle_cex_order(const ExecutionOrder& order, const CexOrderDetails& details);
    
    /**
     * @brief Handle AMM swap orders
     * @param order The execution order
     * @param details AMM swap details
     */
    void handle_amm_swap(const ExecutionOrder& order, const AmmSwapDetails& details);
    
    /**
     * @brief Handle CLMM swap orders
     * @param order The execution order
     * @param details CLMM swap details
     */
    void handle_clmm_swap(const ExecutionOrder& order, const ClmmSwapDetails& details);
    
    /**
     * @brief Handle on-chain transfer orders
     * @param order The execution order
     * @param details Transfer details
     */
    void handle_transfer(const ExecutionOrder& order, const TransferDetails& details);
    
    /**
     * @brief Handle order cancellation requests
     * @param order The execution order
     * @param details Cancellation details
     */
    void handle_cancel(const ExecutionOrder& order, const CancelDetails& details);
    
    /**
     * @brief Handle order replacement/modification requests
     * @param order The execution order
     * @param details Replacement details
     */
    void handle_replace(const ExecutionOrder& order, const ReplaceDetails& details);
    /// @}
    
    /// @name Message Publishing
    /// @{
    /**
     * @brief Publish execution report to strategies
     * @param report The execution report to publish
     */
    void publish_execution_report(const ExecutionReport& report);
    
    /**
     * @brief Publish fill report to strategies
     * @param fill The fill report to publish
     */
    void publish_fill(const Fill& fill);
    /// @}
    
    /// @name DEX Gateway Communication
    /// @{
    /**
     * @brief Call Hummingbot Gateway API
     * @param endpoint API endpoint path
     * @param json_payload JSON request payload
     * @return API response as JSON string
     */
    std::string call_gateway_api(const std::string& endpoint, const std::string& json_payload);
    /// @}
    
    /// @name Market Data Subscription
    /// @{
    /**
     * @brief Subscribe to market data from exchange
     * @param exchange Exchange identifier
     * @param instrument Trading instrument/symbol
     */
    void subscribe_market_data(const std::string& exchange, const std::string& instrument);
    /// @}
    
    /// @name Backtest Execution Engine
    /// @{
    /**
     * @brief Simulate order execution in backtest mode
     * @param order The order to simulate
     */
    void simulate_order_execution(const ExecutionOrder& order);
    
    /**
     * @brief Determine if order should be filled based on market conditions
     * @param order The order to check
     * @param orderbook Current order book state
     * @return true if order should be filled, false otherwise
     */
    bool should_fill_order(const ExecutionOrder& order, const PreprocessedOrderbook& orderbook);
    
    /**
     * @brief Calculate realistic fill price for order
     * @param order The order being filled
     * @param orderbook Current order book state
     * @return Calculated fill price including slippage
     */
    double calculate_fill_price(const ExecutionOrder& order, const PreprocessedOrderbook& orderbook);
    
    /**
     * @brief Generate fill report from simulated execution
     * @param order The original order
     * @param fill_price Calculated fill price
     * @param fill_size Fill quantity
     */
    void generate_fill_from_market_data(const ExecutionOrder& order, double fill_price, double fill_size);
    /// @}
    
    /// @name Market Data Processing
    /// @{
    /**
     * @brief Process incoming preprocessed trade data
     * @param trade Preprocessed trade data
     */
    void process_preprocessed_trade(const PreprocessedTrade& trade);
    
    /**
     * @brief Process incoming preprocessed order book data
     * @param orderbook Preprocessed order book data
     */
    void process_preprocessed_orderbook(const PreprocessedOrderbook& orderbook);
    
    /**
     * @brief Update internal market state tracking
     * @param symbol Trading symbol
     * @param orderbook Latest order book data
     */
    void update_market_state(const std::string& symbol, const PreprocessedOrderbook& orderbook);
    /// @}
    
    /// @name Message Parsing and Serialization
    /// @{
    /**
     * @brief Parse JSON message to ExecutionOrder
     * @param json_message JSON string to parse
     * @return Parsed ExecutionOrder object
     */
    ExecutionOrder parse_execution_order(const std::string& json_message);
    
    /**
     * @brief Parse JSON message to PreprocessedTrade
     * @param json_message JSON string to parse
     * @return Parsed PreprocessedTrade object
     */
    PreprocessedTrade parse_preprocessed_trade(const std::string& json_message);
    
    /**
     * @brief Parse JSON message to PreprocessedOrderbook
     * @param json_message JSON string to parse
     * @return Parsed PreprocessedOrderbook object
     */
    PreprocessedOrderbook parse_preprocessed_orderbook(const std::string& json_message);
    
    /**
     * @brief Serialize ExecutionReport to JSON
     * @param report ExecutionReport to serialize
     * @return JSON string representation
     */
    std::string serialize_execution_report(const ExecutionReport& report);
    
    /**
     * @brief Serialize Fill to JSON
     * @param fill Fill to serialize
     * @return JSON string representation
     */
    std::string serialize_fill(const Fill& fill);
    /// @}
    
    /// @name Utility Functions
    /// @{
    /**
     * @brief Get current time in nanoseconds
     * @return Current timestamp in nanoseconds
     */
    uint64_t get_current_time_ns();
    
    /**
     * @brief Generate unique execution ID
     * @return Unique execution identifier string
     */
    std::string generate_exec_id();
    
    /**
     * @brief Check if order is duplicate (idempotency)
     * @param cl_id Client order ID to check
     * @return true if duplicate, false if new
     */
    bool is_duplicate_order(const std::string& cl_id);
    
    /**
     * @brief Mark order as processed (idempotency tracking)
     * @param cl_id Client order ID to mark
     */
    void mark_order_processed(const std::string& cl_id);
    /// @}
    
    /// @name ZeroMQ Components
    /// @{
    std::unique_ptr<zmq::context_t> zmq_context_;               ///< ZeroMQ context for socket management
    std::unique_ptr<zmq::socket_t> order_receiver_socket_;      ///< PULL socket for receiving orders
    std::unique_ptr<zmq::socket_t> report_publisher_socket_;    ///< PUB socket for publishing reports/fills
    std::unique_ptr<zmq::socket_t> trade_subscriber_socket_;    ///< SUB socket for preprocessed trades
    std::unique_ptr<zmq::socket_t> orderbook_subscriber_socket_; ///< SUB socket for preprocessed orderbook
    /// @}
    
    /// @name CCAPI Components
    /// @{
    std::unique_ptr<ccapi::Session> ccapi_session_;             ///< CCAPI session for exchange connectivity
    std::unique_ptr<ccapi::SessionOptions> session_options_;    ///< CCAPI session configuration options
    std::unique_ptr<ccapi::SessionConfigs> session_configs_;    ///< CCAPI session configurations
    /// @}
    
    /// @name Threading Components
    /// @{
    std::unique_ptr<std::thread> order_receiver_thread_;        ///< Thread for order reception
    std::unique_ptr<std::thread> publisher_thread_;             ///< Thread for message publishing
    std::unique_ptr<std::thread> trade_subscriber_thread_;      ///< Thread for trade data subscription
    std::unique_ptr<std::thread> orderbook_subscriber_thread_;  ///< Thread for orderbook data subscription
    std::atomic<bool> running_;                                 ///< Atomic flag for service state
    /// @}
    
    /// @name Configuration Parameters
    /// @{
    std::string order_endpoint_;                                ///< Order receiver endpoint (tcp://127.0.0.1:5601)
    std::string report_endpoint_;                               ///< Report publisher endpoint (tcp://127.0.0.1:5602)
    std::string gateway_base_url_;                              ///< Hummingbot Gateway base URL
    std::string market_data_host_;                              ///< Market data host configuration
    std::string trade_endpoint_;                                ///< Preprocessed trades endpoint
    std::string orderbook_endpoint_;                            ///< Preprocessed orderbook endpoint
    /// @}
    
    /// @name Order Tracking and State Management
    /// @{
    std::unordered_map<std::string, ccapi::Subscription> active_subscriptions_; ///< Active market data subscriptions
    std::unordered_map<std::string, ExecutionOrder> pending_orders_;            ///< Pending orders (cl_id -> order)
    std::unordered_map<std::string, std::string> cex_order_mapping_;            ///< CEX order mapping (cl_id -> exchange_order_id)
    std::unordered_set<std::string> processed_orders_;                          ///< Processed orders set (idempotency tracking)
    /// @}
    
    /// @name Thread-Safe Message Publishing
    /// @{
    std::queue<std::string> publish_queue_;                     ///< Message queue for publishing
    std::mutex publish_mutex_;                                  ///< Mutex protecting publish queue
    std::condition_variable publish_cv_;                        ///< Condition variable for publisher thread
    /// @}
    
    /// @name Market State Tracking
    /// @{
    std::unordered_map<std::string, PreprocessedOrderbook> latest_orderbook_; ///< Latest orderbook per symbol
    std::unordered_map<std::string, PreprocessedTrade> latest_trade_;         ///< Latest trade per symbol
    std::mutex market_state_mutex_;                             ///< Mutex protecting market state data
    /// @}
    
    /// @name Backtest Execution Settings
    /// @{
    bool backtest_mode_;                                        ///< Enable simulated execution mode
    double fill_probability_;                                   ///< Order fill probability (0.0-1.0)
    double slippage_bps_;                                       ///< Additional execution slippage (basis points)
    /// @}
};

} // namespace latentspeed
