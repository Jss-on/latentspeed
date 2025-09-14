/**
 * @file trading_engine_service.h
 * @brief Ultra-low latency trading engine service optimized for HFT
 * @author jessiondiwangan@gmail.com
 * @date 2025
 * 
 * HFT-OPTIMIZED VERSION - Features:
 * - Lock-free SPSC ring buffers for message passing
 * - Memory pools with pre-allocated cache-aligned objects  
 * - Fixed-size strings to eliminate dynamic allocation
 * - Cache-friendly flat maps for order storage
 * - Atomic counters for real-time performance monitoring
 * - CPU affinity and real-time thread scheduling
 * - Sub-microsecond order processing latency
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
#include <string_view>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

// Linux-specific includes for CPU affinity and real-time scheduling
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

// Convenient logging macros with automatic function name and location
#define LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)

// ZeroMQ
#include <zmq.hpp>

// JSON processing
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

// HFT-optimized data structures
#include "hft_data_structures.h"

// Exchange client interface
#include "exchange/exchange_client.h"
#include "exchange/bybit_client.h"

/**
 * @namespace latentspeed
 * @brief Main namespace for Latentspeed trading infrastructure
 */
namespace latentspeed {

// HFT structures are already declared in hft_data_structures.h
// No forward declarations needed since the header is included above

/**
 * @brief CPU usage modes for adaptive performance tuning
 */
enum class CpuMode {
    NORMAL,     // Balanced CPU usage with moderate sleeps  
    HIGH_PERF,  // 100% CPU usage, minimal latency
    ECO         // CPU-friendly with longer sleeps
};

/**
 * @struct ExecutionOrder
 * @brief Legacy order structure for backward compatibility
 * 
 * This structure is maintained for API compatibility but internally
 * the engine uses HFTExecutionOrder for optimal performance.
 */
struct ExecutionOrder {
    int version = 1;                 ///< Protocol version for compatibility
    std::string cl_id;               ///< Client order ID (unique); idempotency key
    std::string action;              ///< Order action: "place", "cancel", "replace"
    std::string venue_type;          ///< Venue category: "cex", "dex", "chain"
    std::string venue;               ///< Specific venue: "bybit", "binance", "uniswap_v3", etc.
    std::string product_type;        ///< Product type: "spot", "perpetual", "amm_swap", "clmm_swap", "transfer"
    std::map<std::string, std::string> details;            ///< Order-specific parameters
    uint64_t ts_ns;                  ///< Order timestamp in nanoseconds
    std::map<std::string, std::string> tags;  ///< Free-form metadata and routing tags
};

/**
 * @struct ExecutionReport
 * @brief Legacy execution report structure for backward compatibility
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
 * @brief Legacy fill structure for backward compatibility
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
 * @class TradingEngineService
 * @brief Ultra-low latency trading engine optimized for high-frequency trading
 * 
 * HFT-Optimized Features:
 * - Lock-free SPSC ring buffers (8192 message capacity)
 * - Pre-allocated memory pools (1024 orders, 2048 reports/fills)
 * - Cache-aligned data structures with 64-byte alignment
 * - Fixed-size strings to eliminate dynamic allocation
 * - Flat maps with O(log n) search and excellent cache locality
 * - Atomic statistics counters for real-time monitoring
 * - CPU core pinning and real-time thread scheduling
 * - Sub-microsecond order processing latency target
 */
class TradingEngineService {
public:
    /**
     * @brief HFT-optimized constructor with pre-warmed memory pools
     */
    explicit TradingEngineService(CpuMode cpu_mode = CpuMode::NORMAL);
    
    /**
     * @brief Destructor with proper cleanup of HFT resources
     */
    ~TradingEngineService();
    
    /**
     * @brief Initialize HFT trading engine with optimized settings
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Start HFT engine with CPU affinity and real-time scheduling
     */
    void start();
    
    /**
     * @brief Stop HFT engine and log performance statistics
     */
    void stop();
    
    /**
     * @brief Check if the service is running
     * @return true if service is running, false otherwise
     */
    bool is_running() const { return running_.load(std::memory_order_acquire); }

    // Legacy exchange client callbacks (for API compatibility)
    void on_order_update(const OrderUpdate& update);
    void on_fill(const FillData& fill);
    void on_exchange_error(const std::string& error);

private:
    /// @name HFT-Optimized Communication Threads
    /// @{
    /**
     * @brief HFT order receiver thread with dedicated CPU core
     */
    void hft_order_receiver_thread();
    
    /**
     * @brief HFT publisher thread using lock-free queue
     */
    void hft_publisher_thread();
    
    /**
     * @brief Real-time statistics monitoring thread
     */
    void stats_monitoring_thread();
    /// @}
    
    /// @name HFT-Optimized Order Processing Methods
    /// @{
    /**
     * @brief Ultra-fast JSON parsing with memory pool allocation
     * @param json_message Zero-copy string view of JSON message
     * @return Pointer to allocated HFTExecutionOrder or nullptr on failure
     */
    hft::HFTExecutionOrder* parse_execution_order_hft(std::string_view json_message);
    
    /**
     * @brief Ultra-low latency order processing with minimal allocations
     * @param order The HFTExecutionOrder to process
     */
    void process_execution_order_hft(const hft::HFTExecutionOrder& order);
    
    /**
     * @brief HFT-optimized CEX order placement
     */
    void place_cex_order_hft(const hft::HFTExecutionOrder& order);
    
    /**
     * @brief HFT-optimized order cancellation
     */
    void cancel_cex_order_hft(const hft::HFTExecutionOrder& order);
    
    /**
     * @brief HFT-optimized order replacement
     */
    void replace_cex_order_hft(const hft::HFTExecutionOrder& order);
    /// @}
    
    /// @name HFT-Optimized Callback Handlers
    /// @{
    /**
     * @brief Ultra-low latency order update callback
     */
    void on_order_update_hft(const OrderUpdate& update);
    
    /**
     * @brief Ultra-low latency fill callback
     */
    void on_fill_hft(const FillData& fill_data);
    
    /**
     * @brief HFT-optimized exchange error callback
     */
    void on_exchange_error_hft(const std::string& error);
    /// @}
    
    /// @name HFT-Optimized Publishing Methods
    /// @{
    /**
     * @brief Lock-free execution report publishing
     */
    void publish_execution_report_hft(const hft::HFTExecutionReport& report);
    
    /**
     * @brief Lock-free fill publishing
     */
    void publish_fill_hft(const hft::HFTFill& fill);
    
    /**
     * @brief Send acceptance report via HFT path
     */
    void send_acceptance_report_hft(const hft::HFTExecutionOrder& order, 
                                   const std::optional<std::string>& exchange_order_id,
                                   const std::string& message);
    
    /**
     * @brief Send rejection report via HFT path
     */
    void send_rejection_report_hft(const hft::HFTExecutionOrder& order,
                                  const std::string& reason_code,
                                  const std::string& reason_text);
    /// @}
    
    /// @name HFT-Optimized Utility Functions
    /// @{
    /**
     * @brief Ultra-fast timestamp generation
     */
    inline uint64_t get_current_time_ns_hft();
    
    /**
     * @brief Fast execution report serialization
     */
    std::string serialize_execution_report_hft(const hft::HFTExecutionReport& report);
    
    /**
     * @brief Fast fill serialization
     */
    std::string serialize_fill_hft(const hft::HFTFill& fill);
    /// @}
    
    /// @name Legacy Methods (for backward compatibility)
    /// @{
    void process_execution_order(const ExecutionOrder& order);
    void place_cex_order(const ExecutionOrder& order);
    void cancel_cex_order(const ExecutionOrder& order);
    void replace_cex_order(const ExecutionOrder& order);
    void send_rejection_report(const ExecutionOrder& order, 
                             const std::string& reason_code,
                             const std::string& reason_text);
    void publish_execution_report(const ExecutionReport& report);
    void publish_fill(const Fill& fill);
    void zmq_order_receiver_thread();
    void zmq_publisher_thread();
    ExecutionOrder parse_execution_order(const std::string& json_message);
    std::string serialize_execution_report(const ExecutionReport& report);
    std::string serialize_fill(const Fill& fill);
    uint64_t get_current_time_ns();
    std::string generate_exec_id();
    bool is_duplicate_order(const std::string& cl_id);
    void mark_order_processed(const std::string& cl_id);
    std::vector<std::string> getExchangesFromConfig() const;
    std::vector<std::string> getSymbolsFromConfig() const;
    std::vector<std::string> getDynamicSymbolsFromExchange(
        const std::string& exchange_name,
        int top_n = 500,
        const std::string& quote_currency = "USDT") const;
    std::vector<std::string> parseCommaSeparated(const std::string& input) const;
    /// @}
    
    /// @name HFT-Optimized Data Structures
    /// @{
    std::unique_ptr<hft::LockFreeSPSCQueue<hft::PublishMessage, 8192>> publish_queue_;     ///< Lock-free message queue
    std::unique_ptr<hft::MemoryPool<hft::HFTExecutionOrder, 1024>> order_pool_;           ///< Pre-allocated order pool
    std::unique_ptr<hft::MemoryPool<hft::HFTExecutionReport, 2048>> report_pool_;         ///< Pre-allocated report pool  
    std::unique_ptr<hft::MemoryPool<hft::HFTFill, 2048>> fill_pool_;                     ///< Pre-allocated fill pool
    std::unique_ptr<hft::FlatMap<hft::OrderId, hft::HFTExecutionOrder*, 1024>> pending_orders_;  ///< Cache-friendly pending orders
    std::unique_ptr<hft::FlatMap<hft::OrderId, uint64_t, 2048>> processed_orders_;        ///< Cache-friendly processed orders tracking
    std::unique_ptr<hft::HFTStats> stats_;                                               ///< Atomic performance counters
    /// @}
    
    /// @name Exchange Client Components
    /// @{
    std::map<std::string, std::unique_ptr<ExchangeClient>> exchange_clients_;            ///< Exchange clients by name
    std::unique_ptr<BybitClient> bybit_client_;                                         ///< Bybit exchange client
    /// @}
    
    /// @name ZeroMQ Components (HFT-Optimized)
    /// @{
    std::unique_ptr<zmq::context_t> zmq_context_;                                       ///< ZeroMQ context for socket management
    std::unique_ptr<zmq::socket_t> order_receiver_socket_;                              ///< PULL socket for receiving orders
    std::unique_ptr<zmq::socket_t> report_publisher_socket_;                            ///< PUB socket for publishing reports/fills
    /// @}
    
    /// @name Threading Components (HFT-Optimized)
    /// @{
    std::unique_ptr<std::thread> order_receiver_thread_;                                ///< Order processing thread (CPU core 0)
    std::unique_ptr<std::thread> publisher_thread_;                                     ///< Publishing thread (CPU core 1) 
    std::unique_ptr<std::thread> stats_thread_;                                         ///< Statistics monitoring thread
    std::atomic<bool> running_;                                                         ///< Atomic flag for service state
    /// @}
    
    /// @name Service Configuration
    /// @{
    std::string order_endpoint_;                                                        ///< Order receiver endpoint (tcp://127.0.0.1:5601)
    std::string report_endpoint_;                                                       ///< Report publisher endpoint (tcp://127.0.0.1:5602)
    CpuMode cpu_mode_;  // CPU usage mode configuration
    /// @}
    
    /// @name Legacy Components (for backward compatibility)
    /// @{
    std::unordered_map<std::string, ExecutionOrder> pending_orders_legacy_;            ///< Legacy pending orders
    std::unordered_set<std::string> processed_orders_legacy_;                          ///< Legacy processed orders set
    std::queue<std::string> publish_queue_legacy_;                                     ///< Legacy publish queue
    std::mutex publish_mutex_;                                                          ///< Legacy mutex protecting publish queue  
    std::condition_variable publish_cv_;                                                ///< Legacy condition variable for publisher thread
    /// @}
};

} // namespace latentspeed
