/**
 * @file trading_engine_service.cpp
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
 */

#include "trading_engine_service.h"
#include "hft_data_structures.h"
#include "action_dispatch.h"
#include "reason_code_mapper.h"
#include "utils/string_utils.h"
#include "utils/hft_utils.h"
#include "engine/order_serializer.h"
#include <filesystem>
#include <signal.h>
#include "adapters/hyperliquid/hyperliquid_connector_adapter.h"
#include "engine/venue_router.h"
#include "core/auth/credentials_resolver.h"
#include "core/symbol/symbol_mapper.h"
#include "core/reasons/reason_mapper.h"
#include "engine/exec_dto.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <random>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <cctype>
#include <optional>
#include <cmath>

#ifdef HFT_LINUX_FEATURES
#include <sys/personality.h>
#ifndef ADDR_NO_RANDOMIZE
#define ADDR_NO_RANDOMIZE 0x0040000
#endif
#endif
#include <sys/mman.h>  // For mlockall
#include <unistd.h>    // For sched_getcpu
#include <sched.h>     // For CPU affinity and real-time scheduling
#include <sys/resource.h> // For process priority
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <unordered_set>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>

// C++20 features for HFT optimization
#include <bit>       // For std::bit_cast if used
#include <span>      // For zero-copy views
#include <concepts>  // For concept constraints

// x86 intrinsics for SIMD and low-level optimizations
#ifdef __x86_64__
#include <immintrin.h>
#include <x86intrin.h>
#endif

// NUMA support
#ifdef HFT_NUMA_SUPPORT
#include <numa.h>
#include <numaif.h>
#endif

namespace latentspeed {

// HFT memory pool sizes - tuned for typical trading workload
namespace pool_config {
    constexpr size_t ORDER_POOL_SIZE = 1024;       // Max concurrent orders
    constexpr size_t REPORT_POOL_SIZE = 2048;      // 2x orders for updates
    constexpr size_t FILL_POOL_SIZE = 2048;        // Match report pool
    constexpr size_t PUBLISH_QUEUE_SIZE = 8192;    // High-water mark for publishing
    constexpr size_t PENDING_ORDERS_SIZE = 1024;   // Match order pool
    constexpr size_t PROCESSED_ORDERS_SIZE = 2048; // Larger for dedupe tracking
} // namespace pool_config

using namespace hft;
using namespace utils;


// HFT utility functions now in utils/hft_utils.h and utils/string_utils.h

// ============================================================================
// HFT-OPTIMIZED TRADING ENGINE SERVICE
// ============================================================================


/**
 * @brief Ultra-low latency constructor with pre-allocated memory pools
 */
TradingEngineService::TradingEngineService(const TradingEngineConfig& config)
    : running_(false)
    , config_(config)
    , order_endpoint_(config.order_endpoint)
    , report_endpoint_(config.report_endpoint)
    , cpu_mode_(config.cpu_mode)
{
    // Validate configuration
    if (!config_.is_valid()) {
        throw std::invalid_argument("Invalid trading engine configuration: exchange, api_key, and api_secret are required");
    }
    
    // ============================================================================
    // REAL-TIME OPTIMIZATIONS (thread-level only; avoid process-wide RT/affinity)
    // ============================================================================

    // Initialize memory pools with NUMA awareness
    publish_queue_ = std::make_unique<LockFreeSPSCQueue<PublishMessage, pool_config::PUBLISH_QUEUE_SIZE>>();
    order_pool_ = std::make_unique<MemoryPool<HFTExecutionOrder, pool_config::ORDER_POOL_SIZE>>();
    report_pool_ = std::make_unique<MemoryPool<HFTExecutionReport, pool_config::REPORT_POOL_SIZE>>();
    fill_pool_ = std::make_unique<MemoryPool<HFTFill, pool_config::FILL_POOL_SIZE>>();
    pending_orders_ = std::make_unique<FlatMap<OrderId, HFTExecutionOrder*, pool_config::PENDING_ORDERS_SIZE>>();
    processed_orders_ = std::make_unique<FlatMap<OrderId, uint64_t, pool_config::PROCESSED_ORDERS_SIZE>>();
    stats_ = std::make_unique<HFTStats>();
    
    // Pre-warm memory pools and page tables
    std::vector<HFTExecutionOrder*> warmup_orders;
    std::vector<HFTExecutionReport*> warmup_reports;
    std::vector<HFTFill*> warmup_fills;
    
    // Allocate and touch memory to ensure pages are resident
    constexpr size_t WARMUP_COUNT = 128; // Increased for better warming
    warmup_orders.reserve(WARMUP_COUNT);
    warmup_reports.reserve(WARMUP_COUNT);
    warmup_fills.reserve(WARMUP_COUNT);
    
    for (size_t i = 0; i < WARMUP_COUNT; ++i) {
        if (auto* order = order_pool_->allocate()) {
            // Touch the memory to ensure it's paged in - use placement new for proper initialization
            new (order) HFTExecutionOrder();
            warmup_orders.push_back(order);
        }
        if (auto* report = report_pool_->allocate()) {
            new (report) HFTExecutionReport();
            warmup_reports.push_back(report);
        }
        if (auto* fill = fill_pool_->allocate()) {
            new (fill) HFTFill();
            warmup_fills.push_back(fill);
        }
    }
    
    // Return warmed memory to pools
    for (auto* ptr : warmup_orders) order_pool_->deallocate(ptr);
    for (auto* ptr : warmup_reports) report_pool_->deallocate(ptr);
    for (auto* ptr : warmup_fills) fill_pool_->deallocate(ptr);

    // Lock pages in memory if possible (requires CAP_IPC_LOCK)
#ifdef __linux__
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
        spdlog::info("[HFT-Engine] Memory locked to prevent paging");
    } else {
        spdlog::warn("[HFT-Engine] Failed to lock memory (requires CAP_IPC_LOCK)");
    }
#endif
    
    // Phase 2: initialize default mappers
    symbol_mapper_ = std::make_unique<DefaultSymbolMapper>();
    reason_mapper_ = std::make_unique<DefaultReasonMapper>();

    spdlog::info("[HFT-Engine] Ultra-low latency trading engine initialized");
    spdlog::info("[HFT-Engine] Memory pools pre-warmed with {} entries", WARMUP_COUNT);
    spdlog::info("[HFT-Engine] Lock-free queues ready");
    spdlog::info("[HFT-Engine] ZMQ endpoints: orders={}, reports={}", order_endpoint_, report_endpoint_);
}

TradingEngineService::~TradingEngineService() {
    stop();
}

/**
 * @brief Initialize HFT trading engine with optimized settings
 */
bool TradingEngineService::initialize() {
    try {
        const auto start_time = std::chrono::high_resolution_clock::now();
        
        spdlog::info("[HFT-Engine] Starting initialization...");
        
        // Initialize ZeroMQ context with optimized settings for low latency
        zmq_context_ = std::make_unique<zmq::context_t>(1);
        
        // Set ZMQ high-water mark to prevent buffering delays
        int hwm = 1000;
        
        // Order receiver socket (optimized for minimal latency)
        order_receiver_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PULL);
        order_receiver_socket_->set(zmq::sockopt::rcvhwm, hwm);
        order_receiver_socket_->set(zmq::sockopt::rcvtimeo, 0); // Non-blocking
        order_receiver_socket_->bind(order_endpoint_);
        
        // Report publisher socket (optimized for throughput)
        report_publisher_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PUB);
        report_publisher_socket_->set(zmq::sockopt::sndhwm, hwm);
        report_publisher_socket_->set(zmq::sockopt::sndtimeo, 0); // Non-blocking
        report_publisher_socket_->bind(report_endpoint_);
        
        // Set socket linger and timeout options for reliable shutdown
        int linger = 0;
        order_receiver_socket_->set(zmq::sockopt::linger, linger);
        report_publisher_socket_->set(zmq::sockopt::linger, linger);
        
        // Initialize exchange adapters (Phase 1 router)
        spdlog::info("[HFT-Engine] Initializing exchange adapters...");
        venue_router_ = std::make_unique<VenueRouter>();

        // Resolve credentials & testnet flag via central resolver
        auto creds = latentspeed::auth::resolve_credentials(
            config_.exchange, config_.api_key, config_.api_secret, config_.live_trade);
        std::string api_key = creds.api_key;
        std::string api_secret = creds.api_secret;
        bool use_testnet = creds.use_testnet;

        spdlog::info("[HFT-Engine] Exchange: {}, Live trading: {}, Testnet: {}",
                     config_.exchange, config_.live_trade, use_testnet);

        // ---- Exchange adapter wiring -------------------------------------------------
        bool wired = false;
        if (config_.exchange == "hyperliquid") {
            if (api_key.empty() || api_secret.empty()) {
                spdlog::error("[HFT-Engine] Missing Hyperliquid credentials. Provide via config or env: LATENTSPEED_HYPERLIQUID_USER_ADDRESS / LATENTSPEED_HYPERLIQUID_PRIVATE_KEY");
                return false;
            }
            // Use Hummingbot-pattern connector via bridge adapter
            auto adapter = std::make_unique<HyperliquidConnectorAdapter>();
            if (!adapter->initialize(api_key, api_secret, use_testnet)) {
                spdlog::error("[HFT-Engine] Failed to initialize Hyperliquid connector with provided credentials");
                return false;
            }
            adapter->set_order_update_callback([this](const OrderUpdate& u) { this->on_order_update_hft(u); });
            adapter->set_fill_callback([this](const FillData& f) { this->on_fill_hft(f); });
            if (!adapter->connect()) {
                spdlog::warn("[HFT-Engine] Hyperliquid connector not connected; will retry connection");
            } else {
                spdlog::info("[HFT-Engine] Hyperliquid connector connected successfully");
            }
            venue_router_->register_adapter(std::move(adapter));
            spdlog::info("[HFT-Engine] Exchange adapter initialized: hyperliquid (Hummingbot pattern)");
            wired = true;
        }

        if (!wired) {
            std::string supported = "hyperliquid";
            throw std::runtime_error("Unsupported exchange: " + config_.exchange + ". Supported: " + supported);
        }

        // ---- Post-connect open-order rehydration (seeds pending_orders_) ------------
        {
            IExchangeAdapter* adapter = venue_router_ ? venue_router_->get(config_.exchange) : nullptr;
            if (!adapter) {
                throw std::runtime_error("No exchange adapter registered for: " + config_.exchange);
            }

            struct Req { std::string category; std::optional<std::string> settle; };
            std::vector<Req> batches = {
                {"linear", std::string("USDT")},
                {"linear", std::string("USDC")},
                {"inverse", std::nullopt},  // coin-settled; API may need baseCoin; best-effort
                {"spot",   std::nullopt},
            };

            size_t inserted_total = 0;

            for (const auto& b : batches) {
                std::optional<std::string> base_coin; // not used here
                std::optional<std::string> symbol;    // not constrained

                auto briefs = adapter->list_open_orders(b.category, symbol, b.settle, base_coin);
                size_t inserted = 0;

                for (const auto& x : briefs) {
                    OrderId key(x.client_order_id.c_str());
                    if (auto* p = pending_orders_->find(key); p && *p) continue;

                    auto* o = order_pool_->allocate();
                    if (!o) {
                        spdlog::warn("[HFT-Engine] order_pool exhausted during rehydration");
                        break;
                    }
                    new (o) HFTExecutionOrder();

                    o->version = 1;
                    o->cl_id.assign(x.client_order_id.c_str());
                    o->venue.assign(config_.exchange.c_str());
                    o->venue_type.assign("cex");
                    o->product_type.assign((x.category == "spot") ? "spot" : "perpetual");
                    o->symbol.assign(x.symbol.c_str());
                    o->side.assign(x.side.c_str());
                    o->order_type.assign(x.order_type.c_str());
                    if (!x.qty.empty()) {
                        try { o->size = std::stod(x.qty); } catch (...) { o->size = 0.0; }
                    }
                    o->reduce_only = x.reduce_only;

                    // Keep exchange order id if present for future cancels/modifies
                    if (auto it = x.extra.find("orderId"); it != x.extra.end() && !it->second.empty()) {
                        o->params.insert(FixedString<32>("exchange_order_id"),
                                         FixedString<64>(it->second.c_str()));
                    }

                    pending_orders_->insert(o->cl_id, o);
                    ++inserted;
                }

                if (inserted > 0) {
                    spdlog::info("[HFT-Engine] Rehydrated {} open {} orders{}",
                                 inserted,
                                 b.category,
                                 b.settle ? fmt::format(" (settle={})", *b.settle) : "");
                }
                inserted_total += inserted;
            }

            spdlog::info("[HFT-Engine] Rehydrated {} open orders into local map after connect", inserted_total);
        }

        // ---- Market data provider (from master) -------------------------------------
        if (config_.enable_market_data) {
            spdlog::info("[HFT-Engine] Initializing market data provider...");

            market_data_provider_ = std::make_unique<MarketDataProvider>(config_.exchange, config_.symbols);
            market_data_callbacks_ = std::make_shared<SimpleMarketDataCallback>();
            market_data_provider_->set_callbacks(market_data_callbacks_);

            if (!market_data_provider_->initialize()) {
                throw std::runtime_error("Failed to initialize market data provider");
            }

            spdlog::info("[HFT-Engine] Market data provider initialized for {} symbols on {}",
                         config_.symbols.size(), config_.exchange);
        }
        
        const auto end_time = std::chrono::high_resolution_clock::now();
        const auto init_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time - start_time).count();
        
        spdlog::info("[HFT-Engine] Initialization complete in {} ns", init_time_ns);
        spdlog::info("[HFT-Engine] Order pool capacity: {}", order_pool_->available());
        spdlog::info("[HFT-Engine] Report pool capacity: {}", report_pool_->available());
        spdlog::info("[HFT-Engine] Fill pool capacity: {}", fill_pool_->available());
        
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[HFT-Engine] Initialization failed: {}", e.what());
        return false;
    }
}

/**
 * @brief Start HFT engine with CPU affinity and real-time scheduling
 */
void TradingEngineService::start() {
    if (running_.exchange(true)) {
        spdlog::warn("[HFT-Engine] Already running");
        return;
    }
    
    // Start threads with CPU affinity for optimal performance
    order_receiver_thread_ = std::make_unique<std::thread>(&TradingEngineService::hft_order_receiver_thread, this);
    publisher_thread_ = std::make_unique<std::thread>(&TradingEngineService::hft_publisher_thread, this);
    stats_thread_ = std::make_unique<std::thread>(&TradingEngineService::stats_monitoring_thread, this);
    
    // Set thread priorities and CPU affinity (Linux-specific)
    #ifdef __linux__
    // Set real-time scheduling for order processing thread
    struct sched_param param;
    param.sched_priority = 80;
    if (pthread_setschedparam(order_receiver_thread_->native_handle(), SCHED_FIFO, &param) != 0) {
        spdlog::warn("[HFT-Engine] Failed to set real-time scheduling for order thread");
    }
    
    // Pin threads to specific CPU cores to avoid cache misses
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    // Avoid CPU0 to prevent starving kernel/network IRQ handling on many systems
    CPU_SET(2, &cpuset); // Core 2 for order processing
    pthread_setaffinity_np(order_receiver_thread_->native_handle(), sizeof(cpu_set_t), &cpuset);
    
    CPU_ZERO(&cpuset);
    CPU_SET(3, &cpuset); // Core 3 for publishing
    pthread_setaffinity_np(publisher_thread_->native_handle(), sizeof(cpu_set_t), &cpuset);
    #endif
    
    // Start market data provider if enabled
    if (config_.enable_market_data && market_data_provider_) {
        spdlog::info("[HFT-Engine] Starting market data provider...");
        market_data_provider_->start();
        spdlog::info("[HFT-Engine] Market data streaming to ZMQ ports 5556 (trades) and 5557 (orderbook)");
    }
    
    spdlog::info("[HFT-Engine] Ultra-low latency service started (RT scheduling + CPU affinity)");
}

void TradingEngineService::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    // Stop market data provider if enabled
    if (config_.enable_market_data && market_data_provider_) {
        spdlog::info("[HFT-Engine] Stopping market data provider...");
        market_data_provider_->stop();
    }
    
    // Wait for all threads to complete
    if (order_receiver_thread_ && order_receiver_thread_->joinable()) {
        order_receiver_thread_->join();
    }
    if (publisher_thread_ && publisher_thread_->joinable()) {
        publisher_thread_->join();
    }
    if (stats_thread_ && stats_thread_->joinable()) {
        stats_thread_->join();
    }
    
    // Log final statistics
    spdlog::info("[HFT-Engine] Final stats - Orders processed: {}, Avg latency: {:.2f} ns", 
                 stats_->orders_processed.load(), stats_->get_average_latency_ns());
    spdlog::info("[HFT-Engine] Ultra-low latency service stopped");
}

/**
 * @brief HFT-optimized order receiver thread with minimal latency
 */
[[gnu::hot]] void TradingEngineService::hft_order_receiver_thread() {
    spdlog::info("[HFT-Engine] HFT order receiver thread started on dedicated core");
    
#ifdef __x86_64__
    // Calibrate TSC for this thread
    calibrate_tsc();
#endif
    
    // Thread-local variables to avoid repeated allocations
    zmq::message_t request;
    request.rebuild(MAX_MESSAGE_LEN); // Pre-allocate max size
    
    // Prefetch frequently accessed data
    prefetch(stats_.get());
    prefetch(order_pool_.get());
    
    while (running_.load(std::memory_order_acquire)) {
        try {
            const uint64_t recv_start = get_current_time_ns_hft();
            
            auto result = order_receiver_socket_->recv(request, zmq::recv_flags::dontwait);
            
            if (result.has_value()) [[likely]] {
                stats_->orders_received.fetch_add(1, std::memory_order_relaxed);
                
                // Zero-copy string view
                std::string_view message(static_cast<char*>(request.data()), request.size());
                
                // Prefetch order pool for upcoming allocation
                prefetch(order_pool_.get());
                
                // Parse and process with minimal allocations
                if (auto* order = parse_execution_order_hft(message)) [[likely]] {
                    // Prefetch pending orders map for lookup
                    prefetch(pending_orders_.get());
                    
                    process_execution_order_hft(*order);
                    
                    const uint64_t recv_end = get_current_time_ns_hft();
                    const uint64_t latency_ns = recv_end - recv_start;
                    
                    stats_->update_latency(latency_ns);
                    stats_->orders_processed.fetch_add(1, std::memory_order_relaxed);
                } else {
                    stats_->orders_rejected.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                // Use adaptive sleep based on CPU mode
                std::this_thread::sleep_for(get_adaptive_sleep(cpu_mode_));
            }
            
        } catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN) [[unlikely]] {
                spdlog::error("[HFT-Engine] ZMQ order receiver error: {}", e.what());
            }
        } catch (const std::exception& e) {
            spdlog::error("[HFT-Engine] Order receiver error: {}", e.what());
            stats_->orders_rejected.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    spdlog::info("[HFT-Engine] HFT order receiver thread stopped");
}

/**
 * @brief HFT-optimized publisher thread using lock-free queue
 */
void TradingEngineService::hft_publisher_thread() {
    spdlog::info("[HFT-Engine] HFT publisher thread started");
    
    PublishMessage msg;
    
    while (running_.load(std::memory_order_acquire)) {
        try {
            if (publish_queue_->try_pop(msg)) {
                // Send topic and payload as separate ZMQ frames for PUB/SUB
                zmq::message_t topic_frame(msg.topic.size());
                std::memcpy(topic_frame.data(), msg.topic.data(), msg.topic.size());
                
                zmq::message_t payload_frame(msg.payload.size());
                std::memcpy(payload_frame.data(), msg.payload.data(), msg.payload.size());
                
                // Non-blocking send
                if (report_publisher_socket_->send(topic_frame, zmq::send_flags::sndmore | zmq::send_flags::dontwait) &&
                    report_publisher_socket_->send(payload_frame, zmq::send_flags::dontwait)) {
                    stats_->messages_published.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                // Queue empty, minimal pause
                std::this_thread::yield();
            }
        } catch (const std::exception& e) {
            spdlog::error("[HFT-Engine] Publisher error: {}", e.what());
        }
    }
    
    spdlog::info("[HFT-Engine] HFT publisher thread stopped");
}

/**
 * @brief Real-time statistics monitoring thread
 */
void TradingEngineService::stats_monitoring_thread() {
    spdlog::info("[HFT-Engine] Stats monitoring thread started");
    
    auto last_stats_time = std::chrono::steady_clock::now();
    uint64_t last_orders_processed = 0;
    
    while (running_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_stats_time).count();
        
        const uint64_t current_orders = stats_->orders_processed.load();
        const uint64_t orders_per_second = elapsed_ms > 0 ? 
            ((current_orders - last_orders_processed) * 1000) / elapsed_ms : 0;
        
        spdlog::info("[HFT-Stats] Orders/sec: {}, Total: {}, Rejected: {}, "
                     "Avg latency: {:.2f} ns, Min: {} ns, Max: {} ns, "
                     "Queue usage: {}, Pool available: O:{} R:{} F:{}",
                     orders_per_second,
                     current_orders,
                     stats_->orders_rejected.load(),
                     stats_->get_average_latency_ns(),
                     stats_->min_processing_latency_ns.load(),
                     stats_->max_processing_latency_ns.load(),
                     publish_queue_->size(),
                     order_pool_->available(),
                     report_pool_->available(),
                     fill_pool_->available());
        
        last_stats_time = now;
        last_orders_processed = current_orders;
    }
    
    spdlog::info("[HFT-Engine] Stats monitoring thread stopped");
}

// ============================================================================
// HFT-OPTIMIZED PARSING AND PROCESSING METHODS
// ============================================================================

/**
 * @brief Ultra-fast JSON parsing with memory pool allocation
 * @param json_message Zero-copy string view of JSON message
 * @return Pointer to allocated HFTExecutionOrder or nullptr on failure
 */
HFTExecutionOrder* TradingEngineService::parse_execution_order_hft(std::string_view json_message) {
    // Allocate from memory pool (pre-allocated, cache-aligned)
    HFTExecutionOrder* order = order_pool_->allocate();
    if (!order) {
        stats_->memory_pool_exhausted.fetch_add(1, std::memory_order_relaxed);
        spdlog::error("[HFT-Engine] Order pool exhausted");
        return nullptr;
    }

    try {
        ExecParsed parsed;
        bool ok = parse_exec_order_json(json_message, parsed);
        if (!ok) {
            order_pool_->deallocate(order);
            return nullptr;
        }

        // Fill HFTExecutionOrder from typed DTO
        order->version = parsed.version;
        order->cl_id.assign(parsed.cl_id.c_str());
        order->action.assign(parsed.action.c_str());
        order->venue_type.assign(parsed.venue_type.c_str());
        order->venue.assign(parsed.venue.c_str());
        order->product_type.assign(parsed.product_type.c_str());
        if (parsed.ts_ns != 0) order->ts_ns.store(parsed.ts_ns, std::memory_order_relaxed);

        order->symbol.assign(parsed.details.symbol.c_str());
        order->side.assign(parsed.details.side.c_str());
        order->order_type.assign(parsed.details.order_type.c_str());
        order->time_in_force.assign(parsed.details.time_in_force.c_str());

        if (parsed.details.price) order->price = *parsed.details.price;
        if (parsed.details.size) order->size = *parsed.details.size;
        if (parsed.details.stop_price) order->stop_price = *parsed.details.stop_price;
        if (parsed.details.reduce_only) order->reduce_only = *parsed.details.reduce_only;

        // params
        for (const auto& kv : parsed.details.params) {
            if (order->params.full()) break;
            order->params.insert(FixedString<32>(kv.first.c_str()), FixedString<64>(kv.second.c_str()));
        }
        for (const auto& kv : parsed.details.cancel) {
            if (order->params.full()) break;
            std::string key = std::string("cancel_") + kv.first;
            order->params.insert(FixedString<32>(key.c_str()), FixedString<64>(kv.second.c_str()));
        }
        for (const auto& kv : parsed.details.replace) {
            if (order->params.full()) break;
            std::string key = std::string("replace_") + kv.first;
            order->params.insert(FixedString<32>(key.c_str()), FixedString<64>(kv.second.c_str()));
        }

        // tags
        for (const auto& kv : parsed.tags) {
            if (order->tags.full()) break;
            order->tags.insert(FixedString<32>(kv.first.c_str()), FixedString<64>(kv.second.c_str()));
        }

        return order;

    } catch (const std::exception& e) {
        order_pool_->deallocate(order);
        spdlog::error("[HFT-Engine] Parse error: {}", e.what());
        return nullptr;
    }
}

/**
 * @brief Ultra-low latency order processing with branch-free dispatch
 * @param order The HFTExecutionOrder to process
 */
[[gnu::hot, gnu::flatten]] void TradingEngineService::process_execution_order_hft(const HFTExecutionOrder& order) {
    const uint64_t process_start_ns = get_current_time_ns_hft();
    
    try {
        const std::string_view action_view = order.action.view();
        if (action_view.empty()) {
            send_rejection_report_hft(order, "missing_action", "Action is required");
            return;
        }

        const bool action_is_alpha = std::all_of(
            action_view.begin(),
            action_view.end(),
            [](unsigned char ch) { return ch < 0x80 && std::isalpha(ch); }
        );
        if (!action_is_alpha) {
            send_rejection_report_hft(order, "invalid_action", "Action contains invalid characters");
            return;
        }

        const std::string action_lower = to_lower_ascii(action_view);
        const auto action_kind = dispatch::decode_action(action_lower);

        using dispatch::ActionKind;
        using ActionHandler = void (TradingEngineService::*)(const HFTExecutionOrder&);

        ActionHandler handler = nullptr;
        switch (action_kind) {
            case ActionKind::Place:
                handler = &TradingEngineService::place_cex_order_hft;
                break;
            case ActionKind::Cancel:
                handler = &TradingEngineService::cancel_cex_order_hft;
                break;
            case ActionKind::Replace:
                handler = &TradingEngineService::replace_cex_order_hft;
                break;
            case ActionKind::Unknown:
            default:
                break;
        }

        if (handler == nullptr) {
            spdlog::warn(
                "[HFT-Engine] Unknown action '{}', rejecting order {}",
                std::string(action_view),
                order.cl_id.c_str()
            );
            send_rejection_report_hft(order, "invalid_action", "Unknown action");
            return;
        }

        // Check for duplicate processing only after we know the action is supported
        const uint64_t current_time_ns = get_current_time_ns_hft();
        const bool already_processed = processed_orders_->find(order.cl_id);
        bool pending_exists = false;
        if (auto* p = pending_orders_->find(order.cl_id); p && *p) {
            pending_exists = true;
        }

        // Resend-friendly dedupe policy:
        // - CANCEL/REPLACE are idempotent: allow re-processing even if already seen.
        // - PLACE: if already processed and still pending, ignore; if not pending, allow re-process.
        bool allow_process = true;
        if (already_processed) {
            if (action_kind == dispatch::ActionKind::Place) {
                if (pending_exists) {
                    spdlog::warn("[HFT-Engine] Duplicate PLACE ignored (still pending): {}", order.cl_id.c_str());
                    allow_process = false;
                } else {
                    spdlog::info("[HFT-Engine] Resubmitting PLACE for {} (not pending anymore)", order.cl_id.c_str());
                }
            }
        }

        if (!allow_process) {
            return;
        }

        processed_orders_->insert(order.cl_id, current_time_ns);

        (this->*handler)(order);

        // Update latency statistics with relaxed ordering
        const uint64_t process_end_ns = get_current_time_ns_hft();
        const uint64_t latency_ns = process_end_ns - process_start_ns;
        stats_->update_latency(latency_ns);

    } catch (const std::exception& e) { [[unlikely]]
        send_rejection_report_hft(order, "processing_error", e.what());
        spdlog::error("[HFT-Engine] Processing error: {}", e.what());
    }
}

/**
 * @brief HFT-optimized CEX order placement
 */
void TradingEngineService::place_cex_order_hft(const HFTExecutionOrder& order) {
    try {
        // Fast adapter lookup via router
        const std::string venue_key = normalize_venue_key(order.venue.view());
        IExchangeAdapter* adapter = venue_router_ ? venue_router_->get(venue_key) : nullptr;
        if (!adapter) {
            send_rejection_report_hft(order, "unknown_venue", "Exchange not supported");
            return;
        }

        // Validate required fields with early exit
        if (order.symbol.empty() || order.side.empty() || order.order_type.empty()) {
            send_rejection_report_hft(order, "missing_parameters", "Missing required parameters");
            return;
        }
        
        // Only validate size for actual order placement (not cancel/modify)
        if (order.action == std::string_view("place") && order.size == 0.0) {
            send_rejection_report_hft(order, "missing_parameters", "Size must be greater than 0 for place orders");
            return;
        }

        // Build order request with stack allocation
        OrderRequest req;
        req.client_order_id = std::string(order.cl_id.view());

        // Normalise symbol and ensure lowercase side/order type
        const std::string product_type_lower = to_lower_ascii(order.product_type.view());
        std::string category = product_type_lower.empty() || product_type_lower == "spot" ? "spot" : "linear";
        // Infer derivatives category if symbol indicates a perpetual and product_type is missing/spot
        if (category == "spot") {
            std::string sym_upper = to_upper_ascii(order.symbol.view());
            if (sym_upper.find(":") != std::string::npos || sym_upper.rfind("-PERP") == sym_upper.size() - 5) {
                category = "linear";
            }
        }
        req.category = category;
        // Always use mapper (initialized in constructor)
        req.symbol = symbol_mapper_->to_compact(order.symbol.view(), order.product_type.view());
        req.side = to_lower_ascii(order.side.view());

        auto order_type_lower = to_lower_ascii(order.order_type.view());
        bool is_stop_order = (order_type_lower == "stop" || order_type_lower == "stop_market");
        bool is_stop_limit = (order_type_lower == "stop_limit");

        if (is_stop_order) {
            req.order_type = "market";
        } else if (is_stop_limit) {
            req.order_type = "limit";
        } else {
            req.order_type = order_type_lower;
        }

        if (req.order_type != "limit" && req.order_type != "market") {
            send_rejection_report_hft(order, "unsupported_type", "Unsupported order type for venue");
            return;
        }

        if (order.size <= 0.0) {
            send_rejection_report_hft(order, "invalid_size", "Size must be greater than zero");
            return;
        }
        req.quantity = format_decimal(order.size);

        if (req.order_type == "limit") {
            if (order.price == 0.0) {
                send_rejection_report_hft(order, "missing_price", "Price required for limit orders");
                return;
            }
            req.price = format_decimal(order.price);
        }

        // Tick/lot enforcement is handled by trading_core's OrderManager; engine does not re‑enforce here.

        auto tif_mapped = map_time_in_force(order.time_in_force.view());
        if (tif_mapped) {
            req.time_in_force = *tif_mapped;
        } else if (req.order_type == "limit") {
            req.time_in_force = "GTC";
        }

        // Merge pass-through params first so we can override selectively when needed
        order.params.for_each([&](const auto& key, const auto& value) {
            if (!req.extra_params.contains(key.c_str())) {
                req.extra_params.emplace(key.c_str(), value.c_str());
            }
        });

        if ((is_stop_order || is_stop_limit) && order.stop_price <= 0.0) {
            send_rejection_report_hft(order, "missing_stop_price", "stop_price required for stop orders");
            return;
        }

        // Handle stop / stop-limit specifics
        if ((is_stop_order || is_stop_limit) && order.stop_price > 0.0) {
            req.extra_params["triggerPrice"] = format_decimal(order.stop_price);

            // Determine trigger direction: 1 for price rising to trigger, 2 for falling
            std::string trigger_direction = "1";
            if (auto* trig = order.tags.find(FixedString<32>("stop_trigger")); trig != nullptr) {
                if (trig->view() == "below") {
                    trigger_direction = "2";
                } else if (trig->view() == "above") {
                    trigger_direction = "1";
                }
            } else {
                trigger_direction = (req.side == "buy") ? "1" : "2";
            }
            req.extra_params["triggerDirection"] = trigger_direction;
            req.extra_params["orderFilter"] = "StopOrder";

            // Stop-limit retains explicit limit price if provided
            if (is_stop_limit && order.price > 0.0) {
                req.price = format_decimal(order.price);
            }
        }

        req.reduce_only = order.reduce_only;
        if (order.reduce_only) {
            // For reduce-only orders, ensure we're only trading perpetuals/futures
            if (order.product_type == std::string_view("spot")) {
                send_rejection_report_hft(order, "invalid_reduce_only", "reduce_only not supported for spot orders");
                return;
            }
            spdlog::info("[HFT-Engine] 🔒 REDUCE-ONLY order: {} {} {} @ {}", 
                        order.cl_id.c_str(), order.side.c_str(), order.size, order.price);
        } else {
            spdlog::trace("[HFT-Engine] Regular order: {} {} {} @ {}", 
                          order.cl_id.c_str(), order.side.c_str(), order.size, order.price);
        }

        // Place order via exchange adapter
        OrderResponse response = adapter->place_order(req);

        if (response.success) {
            // Store pending order for tracking
            auto* order_copy = order_pool_->allocate();
            if (order_copy) {
                *order_copy = order;
                if (response.exchange_order_id.has_value()) {
                    order_copy->params.insert(FixedString<32>("exchange_order_id"),
                                             FixedString<64>(response.exchange_order_id->c_str()));
                }
                pending_orders_->insert(order.cl_id, order_copy);
            }

            send_acceptance_report_hft(order, response.exchange_order_id, response.message);
        } else {
            send_rejection_report_hft(order, "exchange_rejected", response.message);
        }

    } catch (const std::exception& e) {
        send_rejection_report_hft(order, "exchange_error", e.what());
        spdlog::error("[HFT-Engine] Order placement error: {}", e.what());
    }
}

/**
 * @brief Find parameter or tag value in order
 */
const FixedString<64>* TradingEngineService::find_order_param_or_tag(
    const HFTExecutionOrder& order,
    const char* key) const {
    if (auto* param = order.params.find(FixedString<32>(key))) {
        return param;
    }
    return order.tags.find(FixedString<32>(key));
}

/**
 * @brief HFT-optimized native cancellation flow
 */
void TradingEngineService::cancel_cex_order_hft(const HFTExecutionOrder& order) {
    try {
        IExchangeAdapter* adapter = venue_router_ ? venue_router_->get(normalize_venue_key(order.venue.view())) : nullptr;
        if (!adapter) {
            send_rejection_report_hft(order, "unknown_venue", "Exchange not supported");
            return;
        }
        auto& client = *adapter; // alias for readability

        const auto* cl_id_to_cancel = find_order_param_or_tag(order, "cancel_cl_id_to_cancel");
        if (!cl_id_to_cancel) {
            send_rejection_report_hft(order, "missing_cancel_id", "Missing cl_id_to_cancel for cancel request");
            return;
        }

        std::optional<std::string> exchange_order_id;
        if (auto* exch = find_order_param_or_tag(order, "cancel_exchange_order_id")) {
            exchange_order_id = std::string(exch->c_str());
        }

        const std::string cl_to_cancel_str = cl_id_to_cancel->c_str();

        std::optional<std::string> symbol;
        if (!order.symbol.empty()) {
            // Always use mapper (initialized in constructor)
            symbol = symbol_mapper_->to_compact(order.symbol.view(), order.product_type.view());
        }

        // Fallback to cached pending order metadata when not supplied in request
        OrderId original_id_lookup(cl_to_cancel_str.c_str());
        if (!symbol.has_value()) {
            if (auto* pending_order = pending_orders_->find(original_id_lookup); pending_order && *pending_order) {
                // Always use mapper (initialized in constructor)
                symbol = symbol_mapper_->to_compact((*pending_order)->symbol.view(), (*pending_order)->product_type.view());
            }
        }
        if (!exchange_order_id.has_value()) {
            if (auto* pending_order = pending_orders_->find(original_id_lookup); pending_order && *pending_order) {
                if (auto* stored_exch = (*pending_order)->params.find(FixedString<32>("exchange_order_id"))) {
                    exchange_order_id = std::string(stored_exch->c_str());
                }
            }
        }

        OrderResponse response = client.cancel_order(cl_to_cancel_str, symbol, exchange_order_id);

        // Use existing utility from utils/string_utils.h
        const std::string msg_lower = to_lower_ascii(response.message);
        const bool idempotent_missing = (!response.success) && (
            msg_lower.find("does not exist") != std::string::npos ||
            msg_lower.find("not exists") != std::string::npos ||
            msg_lower.find("not found") != std::string::npos ||
            msg_lower.find("unknown order") != std::string::npos
        );

        if (response.success || idempotent_missing) {
            // 1) Acknowledge the cancel request itself
            send_acceptance_report_hft(order, response.exchange_order_id, response.message.empty() ?
                                       "Cancel request sent" : response.message);

            // 2) Publish synthetic 'canceled' for the ORIGINAL order so upstream can clear state
            auto* report = report_pool_->allocate();
            if (report) {
                report->version = 1;
                report->cl_id = original_id_lookup;
                report->exchange_order_id.assign(response.exchange_order_id.has_value() ? response.exchange_order_id->c_str() : "");
                report->status.assign("canceled");
                report->reason_code.assign("ok");
                report->reason_text.assign(idempotent_missing ? "Cancel idempotent: order missing" : "Cancel accepted");
                report->ts_ns.store(get_current_time_ns_hft(), std::memory_order_relaxed);
                // Copy tags from pending original if present
                if (auto* pending_order = pending_orders_->find(original_id_lookup); pending_order && *pending_order) {
                    report->tags = (*pending_order)->tags;
                }
                publish_execution_report_hft(*report);
                report_pool_->deallocate(report);
            }

            // 3) Drop any local pending cache for the original order
            if (auto* pending_order = pending_orders_->find(original_id_lookup); pending_order && *pending_order) {
                order_pool_->deallocate(*pending_order);
                pending_orders_->erase(original_id_lookup);
            }
        } else {
            send_rejection_report_hft(order, "cancel_rejected", response.message);
        }

    } catch (const std::exception& e) {
        send_rejection_report_hft(order, "exchange_error", e.what());
        spdlog::error("[HFT-Engine] Order closing error: {}", e.what());
    }
}

/**
 * @brief HFT-optimized order replacement
 */
void TradingEngineService::replace_cex_order_hft(const HFTExecutionOrder& order) {
    try {
        IExchangeAdapter* adapter = venue_router_ ? venue_router_->get(normalize_venue_key(order.venue.view())) : nullptr;
        if (!adapter) {
            send_rejection_report_hft(order, "unknown_venue", "Exchange not supported");
            return;
        }
        auto& client = *adapter;

        const auto* cl_id_to_replace = find_order_param_or_tag(order, "replace_cl_id_to_replace");
        if (!cl_id_to_replace) {
            send_rejection_report_hft(order, "missing_replace_id", "Missing cl_id_to_replace");
            return;
        }

        std::optional<std::string> new_price;
        std::optional<std::string> new_quantity;

        if (auto* price_override = find_order_param_or_tag(order, "replace_new_price")) {
            if (!price_override->empty()) {
                new_price = price_override->c_str();
            }
        } else if (order.price > 0.0) {
            new_price = format_decimal(order.price);
        }

        if (auto* size_override = find_order_param_or_tag(order, "replace_new_size")) {
            if (!size_override->empty()) {
                new_quantity = size_override->c_str();
            }
        } else if (order.size > 0.0) {
            new_quantity = format_decimal(order.size);
        }

        OrderResponse response = client.modify_order(
            std::string(cl_id_to_replace->view()), new_quantity, new_price);

        if (response.success) {
            send_acceptance_report_hft(order, std::nullopt, "Order modified");
        } else {
            send_rejection_report_hft(order, "modify_rejected", response.message);
        }

    } catch (const std::exception& e) {
        send_rejection_report_hft(order, "exchange_error", e.what());
        spdlog::error("[HFT-Engine] Modify error: {}", e.what());
    }
}

// ============================================================================
// HFT-OPTIMIZED CALLBACK HANDLERS
// ============================================================================

/**
 * @brief Ultra-low latency order update callback
 */
void TradingEngineService::on_order_update_hft(const OrderUpdate& update) {
    try {
        // Fast order lookup in pending orders
        OrderId cl_id(update.client_order_id.c_str());
        auto* order_ptr = pending_orders_->find(cl_id);

        if (!order_ptr || !*order_ptr) {
            // Unknown order
            if (is_terminal_status(update.status)) {
                // Late terminal update: idempotent; safe to ignore
                spdlog::info("[HFT-Engine] Late terminal update for non-tracked order: {} status={}",
                             update.client_order_id, update.status);
                return;
            }

            // Non-terminal unknown update → try lazy rehydration from exchange
            IExchangeAdapter* client_raw = nullptr;
            if (venue_router_) {
                // Prefer configured exchange key if present
                client_raw = venue_router_->get(config_.exchange);
                if (!client_raw) {
                    // Fallback: if only one adapter was registered, take it
                    // (We don't have an iterator API; this code path is best-effort.)
                }
            }

            if (client_raw) {
                OrderResponse qr = client_raw->query_order(update.client_order_id);
                if (qr.success) {
                    // Expect symbol/category in extra_data
                    std::string sym;
                    std::string cat;
                    if (auto it = qr.extra_data.find("symbol"); it != qr.extra_data.end()) {
                        sym = it->second;
                    }
                    if (auto it = qr.extra_data.find("category"); it != qr.extra_data.end()) {
                        cat = it->second;
                    }

                    if (!sym.empty()) {
                        auto* o = order_pool_->allocate();
                        if (o) {
                            new (o) HFTExecutionOrder(); // placement new
                            o->version = 1;
                            o->cl_id.assign(update.client_order_id.c_str());
                            o->venue.assign(client_raw->get_exchange_name().c_str()); // venue from the queried client
                            o->venue_type.assign("cex");
                            // category → product_type
                            // spot → "spot"; otherwise treat as perpetual
                            if (!cat.empty() && to_lower_ascii(cat) == "spot") {
                                o->product_type.assign("spot");
                            } else {
                                o->product_type.assign("perpetual");
                            }
                            o->symbol.assign(sym.c_str());

                            // If exchange_order_id came back, store it in params for later use
                            if (qr.exchange_order_id.has_value()) {
                                o->params.insert(FixedString<32>("exchange_order_id"),
                                                 FixedString<64>(qr.exchange_order_id->c_str()));
                            }

                            pending_orders_->insert(o->cl_id, o);
                            // refresh local pointer after insert
                            order_ptr = pending_orders_->find(cl_id);
                            spdlog::info("[HFT-Engine] Lazy rehydrated live order {}", update.client_order_id);
                        }
                    } else {
                        spdlog::warn("[HFT-Engine] Lazy rehydrate failed: query_order({}) had no symbol",
                                     update.client_order_id);
                    }
                } else {
                    spdlog::warn("[HFT-Engine] Lazy rehydrate failed: query_order({}) not open/success",
                                 update.client_order_id);
                }
            } else {
                spdlog::warn("[HFT-Engine] Update for unknown live order (no client available): {} status={}",
                             update.client_order_id, update.status);
            }

            // If still unknown after rehydrate attempt, bail out (non-terminal)
            if (!order_ptr || !*order_ptr) {
                return;
            }
        }

        // From here, we have a cached order entry (original or placeholder)
        const HFTExecutionOrder& original_order = **order_ptr;

        const std::string raw_status = update.status;
        auto normalized_status = normalize_report_status(raw_status);
        if (!normalized_status) {
            return;
        }

        auto* report = report_pool_->allocate();
        if (!report) {
            spdlog::error("[HFT-Engine] Report pool exhausted");
            return;
        }

        report->version = 1;
        report->cl_id = cl_id;
        report->exchange_order_id.assign(update.exchange_order_id.c_str());
        report->status.assign(normalized_status->c_str());

        // Always use reason_mapper_ (initialized in constructor)
        auto mapped = reason_mapper_->map(*normalized_status, update.reason);
        report->reason_code.assign(mapped.reason_code.c_str());
        report->reason_text.assign(mapped.reason_text.c_str());
        report->ts_ns.store(get_current_time_ns_hft(), std::memory_order_relaxed);
        report->tags = original_order.tags; // placeholder will have empty tags, which is fine

        // Ensure core tags are present for downstream (trading_core) routing
        if (report->tags.find(FixedString<32>("venue")) == nullptr && !original_order.venue.empty()) {
            report->tags.insert(FixedString<32>("venue"), FixedString<64>(original_order.venue.c_str()));
        }
        // Publish via lock-free queue
        publish_execution_report_hft(*report);
        report_pool_->deallocate(report);

        // Clean up completed orders
        if (is_terminal_status(raw_status)) {
            pending_orders_->erase(cl_id);
            order_pool_->deallocate(*order_ptr);
        }

    } catch (const std::exception& e) {
        spdlog::error("[HFT-Engine] Order update error: {}", e.what());
    }
}


/**
 * @brief Ultra-low latency fill callback
 */
void TradingEngineService::on_fill_hft(const FillData& fill_data) {
    try {
        // Allocate fill from pool
        auto* fill = fill_pool_->allocate();
        if (!fill) {
            spdlog::error("[HFT-Engine] Fill pool exhausted");
            return;
        }

        // Fast string-to-double conversion
        fill->version = 1;
        {
            const std::string& primary = fill_data.client_order_id.empty()
                ? fill_data.exchange_order_id
                : fill_data.client_order_id;
            fill->cl_id.assign(primary.c_str());
        }
        fill->exchange_order_id.assign(fill_data.exchange_order_id.c_str());
        fill->exec_id.assign(fill_data.exec_id.c_str());
        fill->symbol_or_pair.assign(fill_data.symbol.c_str());
        fill->price = std::stod(fill_data.price);
        fill->size = std::stod(fill_data.quantity);
        fill->fee_amount = std::stod(fill_data.fee);
        fill->fee_currency.assign(fill_data.fee_currency.c_str());
        fill->liquidity.assign(fill_data.liquidity.c_str());
        fill->ts_ns.store(get_current_time_ns_hft(), std::memory_order_relaxed);

        // Copy tags from original order if available
        OrderId lookup_id(fill->cl_id.c_str());
        bool is_perp = false;
        if (auto* order_ptr = pending_orders_->find(lookup_id); order_ptr && *order_ptr) {
            fill->tags = (*order_ptr)->tags;
            if (!(*order_ptr)->symbol.empty()) {
                fill->symbol_or_pair.assign((*order_ptr)->symbol.c_str());
            }
            fill->tags.insert(FixedString<32>("execution_type"), FixedString<64>("live"));
            // Ensure venue tag is present for downstream analytics
            if (fill->tags.find(FixedString<32>("venue")) == nullptr && !(*order_ptr)->venue.empty()) {
                fill->tags.insert(FixedString<32>("venue"), FixedString<64>((*order_ptr)->venue.c_str()));
            }
            // Determine perp from original order's product_type
            std::string pt = to_lower_ascii((*order_ptr)->product_type.view());
            is_perp = (pt == "perpetual");
        } else {
            // External/manual action: still publish with clear tagging
            fill->tags.insert(FixedString<32>("execution_type"), FixedString<64>("external"));
            // Infer perp if ccxt style present (BASE/QUOTE:SETTLE)
            std::string cur = std::string(fill->symbol_or_pair.c_str());
            is_perp = (cur.find(':') != std::string::npos);
        }

        // Normalize to hyphen symbol (always use mapper, initialized in constructor)
        std::string hyphen = symbol_mapper_->to_hyphen(fill->symbol_or_pair.c_str(), is_perp);
        fill->symbol_or_pair.assign(hyphen.c_str());

        publish_fill_hft(*fill);
        stats_->fills_received.fetch_add(1, std::memory_order_relaxed);

        spdlog::info("[HFT-Engine] Fill: {} {} @ {} (fee: {} {})", 
                     fill->cl_id.c_str(), fill->size, fill->price, 
                     fill->fee_amount, fill->fee_currency.c_str());

        fill_pool_->deallocate(fill);

    } catch (const std::exception& e) {
        spdlog::error("[HFT-Engine] Fill processing error: {}", e.what());
    }
}

/**
 * @brief HFT-optimized exchange error callback
 */
void TradingEngineService::on_exchange_error_hft(const std::string& error) {
    spdlog::error("[HFT-Engine] Exchange error: {}", error);
    
    // Could implement error message publishing here if needed
}

// ============================================================================
// HFT-OPTIMIZED PUBLISHING METHODS
// ============================================================================

/**
 * @brief Lock-free execution report publishing
 */
void TradingEngineService::publish_execution_report_hft(const HFTExecutionReport& report) {
    try {
        // Serialize with stack-allocated buffer
        std::string json_report = serialize_execution_report_hft(report);
        
        // Create message for lock-free queue
        PublishMessage msg(MessageType::EXECUTION_REPORT, "exec.report", json_report);
        msg.timestamp_ns = get_current_time_ns_hft();
        
        if (!publish_queue_->try_push(msg)) {
            stats_->queue_full_count.fetch_add(1, std::memory_order_relaxed);
            spdlog::warn("[HFT-Engine] Publish queue full, dropping execution report");
        }
        
    } catch (const std::exception& e) {
        spdlog::error("[HFT-Engine] Error publishing execution report: {}", e.what());
    }
}

/**
 * @brief Lock-free fill publishing
 */
void TradingEngineService::publish_fill_hft(const HFTFill& fill) {
    try {
        std::string json_fill = serialize_fill_hft(fill);
        
        PublishMessage msg(MessageType::FILL, "exec.fill", json_fill);
        msg.timestamp_ns = get_current_time_ns_hft();
        
        if (!publish_queue_->try_push(msg)) {
            stats_->queue_full_count.fetch_add(1, std::memory_order_relaxed);
            spdlog::warn("[HFT-Engine] Publish queue full, dropping fill");
        }
        
    } catch (const std::exception& e) {
        spdlog::error("[HFT-Engine] Error publishing fill: {}", e.what());
    }
}

/**
 * @brief Send acceptance report via HFT path
 */
void TradingEngineService::send_acceptance_report_hft(const HFTExecutionOrder& order, 
                                                     const std::optional<std::string>& exchange_order_id,
                                                     const std::string& message) {
    auto* report = report_pool_->allocate();
    if (!report) return;
    
    report->version = 1;
    report->cl_id = order.cl_id;
    report->status.assign("accepted");
    if (exchange_order_id) {
        report->exchange_order_id.assign(exchange_order_id->c_str());
    }
    report->reason_code.assign("ok");
    report->reason_text.assign(message);
    report->ts_ns.store(get_current_time_ns_hft(), std::memory_order_relaxed);
    report->tags = order.tags;
    // Ensure venue tag present for downstream consumers
    if (report->tags.find(FixedString<32>("venue")) == nullptr && !order.venue.empty()) {
        report->tags.insert(FixedString<32>("venue"), FixedString<64>(order.venue.c_str()));
    }
    
    publish_execution_report_hft(*report);
    report_pool_->deallocate(report);
}

/**
 * @brief Send rejection report via HFT path
 */
void TradingEngineService::send_rejection_report_hft(const HFTExecutionOrder& order,
                                                    const std::string& reason_code,
                                                    const std::string& reason_text) {
    auto* report = report_pool_->allocate();
    if (!report) return;
    
    report->version = 1;
    report->cl_id = order.cl_id;
    report->status.assign("rejected");
    const std::string canonical = latentspeed::exec::canonical_reason_code(reason_code);
    report->reason_code.assign(canonical.c_str());
    report->reason_text.assign(reason_text);
    report->ts_ns.store(get_current_time_ns_hft(), std::memory_order_relaxed);
    report->tags = order.tags;
    if (report->tags.find(FixedString<32>("venue")) == nullptr && !order.venue.empty()) {
        report->tags.insert(FixedString<32>("venue"), FixedString<64>(order.venue.c_str()));
    }
    
    publish_execution_report_hft(*report);
    report_pool_->deallocate(report);
}

// ============================================================================
// HFT-OPTIMIZED UTILITY FUNCTIONS
// ============================================================================

/**
 * @brief Ultra-fast timestamp generation using TSC when available
 */
[[gnu::hot, gnu::flatten]] inline uint64_t TradingEngineService::get_current_time_ns_hft() {
#ifdef __x86_64__
    // Use TSC for ultra-low latency (2-3 cycles)
    if (get_tsc_to_ns_scale() > 0.0) [[likely]] {
        return static_cast<uint64_t>(rdtsc() * get_tsc_to_ns_scale());
    }
#endif
    // Fallback to standard chrono
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

/**
 * @brief Fast execution report serialization
 */
std::string TradingEngineService::serialize_execution_report_hft(const HFTExecutionReport& report) {
    return engine::serialize_execution_report(report);
}

/**
 * @brief Fast fill serialization
 */
std::string TradingEngineService::serialize_fill_hft(const HFTFill& fill) {
    return engine::serialize_fill(fill);
}

} // namespace latentspeed
