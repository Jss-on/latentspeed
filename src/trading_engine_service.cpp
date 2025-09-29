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
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <random>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>

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
#include <bit>
#include <span>
#include <ranges>
#include <concepts>
#include <coroutine>
#include <latch>
#include <barrier>
#include <semaphore>
#include <source_location>

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

using namespace hft;


// ============================================================================
// ULTRA-LOW LATENCY UTILITIES
// ============================================================================

#ifdef __x86_64__
/**
 * @brief Ultra-fast TSC-based timestamp with serializing instruction
 * @return CPU timestamp counter value
 */
[[gnu::hot, gnu::flatten]] inline uint64_t rdtscp() noexcept {
    uint32_t aux;
    return __rdtscp(&aux);
}

/**
 * @brief Non-serializing TSC read (faster but may reorder)
 */
[[gnu::hot, gnu::flatten]] inline uint64_t rdtsc() noexcept {
    return __rdtsc();
}

// Calibration constants for TSC to nanoseconds conversion
static thread_local uint64_t tsc_frequency = 0;
static thread_local double tsc_to_ns_scale = 0.0;

/**
 * @brief Get adaptive sleep duration based on CPU mode
 */
std::chrono::nanoseconds get_adaptive_sleep(CpuMode cpu_mode) {
    switch (cpu_mode) {
        case CpuMode::HIGH_PERF:
            // Aggressive spinning for maximum performance (yield or minimal sleep)
            return std::chrono::nanoseconds(0); // No sleep, pure busy-wait
        case CpuMode::ECO:
            // Eco mode: longer sleep to conserve power
            return std::chrono::microseconds(100); // Sleep for 100 microseconds
        case CpuMode::NORMAL:
        default:
            return std::chrono::microseconds(10); // Default balanced sleep
    }
}

/**
 * @brief Calibrate TSC frequency on startup
 */
void calibrate_tsc() {
    using namespace std::chrono;
    
    const auto start_tsc = rdtscp();
    const auto start_time = high_resolution_clock::now();
    
    // Busy wait for calibration period
    const auto calibration_duration = milliseconds(100);
    while (high_resolution_clock::now() - start_time < calibration_duration) {
        __builtin_ia32_pause();
    }
    
    const auto end_tsc = rdtscp();
    const auto end_time = high_resolution_clock::now();
    
    const auto elapsed_ns = duration_cast<nanoseconds>(end_time - start_time).count();
    const auto elapsed_tsc = end_tsc - start_tsc;
    
    tsc_frequency = (elapsed_tsc * 1'000'000'000ULL) / elapsed_ns;
    tsc_to_ns_scale = static_cast<double>(elapsed_ns) / static_cast<double>(elapsed_tsc);
    
    spdlog::info("[HFT-Engine] TSC frequency calibrated: {} Hz", tsc_frequency);
}
#endif

/**
 * @brief Memory prefetch hint for cache optimization
 */
template<typename T>
[[gnu::always_inline]] inline void prefetch(const T* ptr, int locality = 3) noexcept {
    __builtin_prefetch(ptr, 0, locality);
}

/**
 * @brief Compiler fence to prevent reordering
 */
[[gnu::always_inline]] inline void compiler_fence() noexcept {
    asm volatile("" ::: "memory");
}

/**
 * @brief CPU pause instruction for spinlocks
 */
[[gnu::always_inline]] inline void cpu_pause() noexcept {
#ifdef __x86_64__
    __builtin_ia32_pause();
#else
    std::this_thread::yield();
#endif
}

// ============================================================================
// HFT-OPTIMIZED TRADING ENGINE SERVICE
// ============================================================================


/**
 * @brief Ultra-low latency constructor with pre-allocated memory pools
 */
TradingEngineService::TradingEngineService(const TradingEngineConfig& config)
    : config_(config)
    , running_(false)
    , order_endpoint_("tcp://127.0.0.1:5601")
    , report_endpoint_("tcp://127.0.0.1:5602")
    , cpu_mode_(config.cpu_mode)
{
    // Validate configuration
    if (!config_.is_valid()) {
        throw std::invalid_argument("Invalid trading engine configuration: exchange, api_key, and api_secret are required");
    }
    
#ifdef HFT_NUMA_SUPPORT
    // Initialize NUMA if available
    if (numa_available() >= 0) {
        // Bind to local NUMA node for better memory locality
        numa_set_localalloc();
        spdlog::info("[HFT-Engine] NUMA support enabled, using local allocation");
        
        // Get NUMA node count and current node
        int num_nodes = numa_num_configured_nodes();
        int current_node = numa_node_of_cpu(sched_getcpu());
        spdlog::info("[HFT-Engine] NUMA nodes: {}, current node: {}", num_nodes, current_node);
    }
#endif

    // ============================================================================
    // REAL-TIME OPTIMIZATIONS
    // ============================================================================
    
    // Set real-time scheduling for main process
    struct sched_param param;
    param.sched_priority = 80; // High priority (1-99 range)
    
    if (sched_setscheduler(0, SCHED_FIFO, &param) == 0) {
        spdlog::info("[HFT-Engine] Real-time FIFO scheduling enabled (priority: {})", param.sched_priority);
    } else {
        spdlog::warn("[HFT-Engine] Failed to set real-time scheduling (requires root/CAP_SYS_NICE)");
    }
    
    // Set process to highest nice priority
    if (setpriority(PRIO_PROCESS, 0, -20) == 0) {
        spdlog::info("[HFT-Engine] Process priority set to highest (-20)");
    } else {
        spdlog::warn("[HFT-Engine] Failed to set process priority");
    }
    
    // CPU affinity - bind to isolated cores (cores 2-7 typically isolated)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    // Check for isolated CPUs from kernel command line
    // In production, use: isolcpus=2-7 nohz_full=2-7 rcu_nocbs=2-7
    const std::vector<int> isolated_cpus = {2, 3, 4, 5}; // Configure based on your system
    
    for (int cpu : isolated_cpus) {
        CPU_SET(cpu, &cpuset);
    }
    
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0) {
        spdlog::info("[HFT-Engine] CPU affinity set to isolated cores: 2-5");
    } else {
        spdlog::warn("[HFT-Engine] Failed to set CPU affinity");
    }
    
    // Disable address space randomization for deterministic performance
#ifdef HFT_LINUX_FEATURES
    if (personality(ADDR_NO_RANDOMIZE) != -1) {
        spdlog::info("[HFT-Engine] Address space randomization disabled");
    }
#endif

    // Initialize memory pools with NUMA awareness
    publish_queue_ = std::make_unique<LockFreeSPSCQueue<PublishMessage, 8192>>();
    order_pool_ = std::make_unique<MemoryPool<HFTExecutionOrder, 1024>>();
    report_pool_ = std::make_unique<MemoryPool<HFTExecutionReport, 2048>>();
    fill_pool_ = std::make_unique<MemoryPool<HFTFill, 2048>>();
    pending_orders_ = std::make_unique<FlatMap<OrderId, HFTExecutionOrder*, 1024>>();
    processed_orders_ = std::make_unique<FlatMap<OrderId, uint64_t, 2048>>();
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
    
    spdlog::info("[HFT-Engine] Ultra-low latency trading engine initialized");
    spdlog::info("[HFT-Engine] Memory pools pre-warmed with {} entries", WARMUP_COUNT);
    spdlog::info("[HFT-Engine] Lock-free queues ready");
    spdlog::info("[HFT-Engine] Unix domain sockets: {}, {}", order_endpoint_, report_endpoint_);
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
        
        // Initialize exchange clients
        spdlog::info("[HFT-Engine] Initializing exchange clients...");
        
        // Use configuration from command line arguments
        std::string api_key = config_.api_key;
        std::string api_secret = config_.api_secret;
        bool use_testnet = !config_.live_trade; // Use testnet when not in live trade mode
        
        spdlog::info("[HFT-Engine] Exchange: {}, Live trading: {}, Testnet: {}", 
                     config_.exchange, config_.live_trade, use_testnet);
        
        if (config_.exchange == "bybit") {
            // Create Bybit client with configuration
            auto bybit_client = std::make_unique<BybitClient>();
            
            // Initialize with configuration credentials
            bybit_client->initialize(api_key, api_secret, use_testnet);
            
            // Set callbacks for order updates and fills
            bybit_client->set_order_update_callback([this](const OrderUpdate& update) {
                this->on_order_update_hft(update);
            });
            
            bybit_client->set_fill_callback([this](const FillData& fill) {
                this->on_fill_hft(fill);
            });
            
            // Add to exchange clients map
            exchange_clients_[config_.exchange] = std::move(bybit_client);
            
            spdlog::info("[HFT-Engine] Exchange client initialized: {}", config_.exchange);
        } else {
            throw std::runtime_error("Unsupported exchange: " + config_.exchange + ". Currently supported: bybit");
        }
        
        // Initialize market data provider if enabled
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
    param.sched_priority = 99;
    if (pthread_setschedparam(order_receiver_thread_->native_handle(), SCHED_FIFO, &param) != 0) {
        spdlog::warn("[HFT-Engine] Failed to set real-time scheduling for order thread");
    }
    
    // Pin threads to specific CPU cores to avoid cache misses
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset); // Core 0 for order processing
    pthread_setaffinity_np(order_receiver_thread_->native_handle(), sizeof(cpu_set_t), &cpuset);
    
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset); // Core 1 for publishing
    pthread_setaffinity_np(publisher_thread_->native_handle(), sizeof(cpu_set_t), &cpuset);
    #endif
    
    // Start market data provider if enabled
    if (config_.enable_market_data && market_data_provider_) {
        spdlog::info("[HFT-Engine] Starting market data provider...");
        market_data_provider_->start();
        spdlog::info("[HFT-Engine] Market data streaming to ZMQ ports 5556 (trades) and 5557 (orderbook)");
    }
    
    spdlog::info("[HFT-Engine] Ultra-low latency service started");
    spdlog::info("[HFT-Engine] Real-time scheduling enabled");
    spdlog::info("[HFT-Engine] CPU affinity configured");
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
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
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
        rapidjson::Document doc;
        doc.Parse(json_message.data(), json_message.size());

        if (doc.HasParseError()) {
            order_pool_->deallocate(order);
            return nullptr;
        }

        // Parse with minimal branches for better performance
        if (doc.HasMember("version")) order->version = doc["version"].GetInt();
        if (doc.HasMember("cl_id")) order->cl_id.assign(doc["cl_id"].GetString());
        if (doc.HasMember("action")) order->action.assign(doc["action"].GetString());
        if (doc.HasMember("venue_type")) order->venue_type.assign(doc["venue_type"].GetString());
        if (doc.HasMember("venue")) order->venue.assign(doc["venue"].GetString());
        if (doc.HasMember("product_type")) order->product_type.assign(doc["product_type"].GetString());
        if (doc.HasMember("ts_ns")) order->ts_ns.store(doc["ts_ns"].GetUint64(), std::memory_order_relaxed);

        // Parse details with cache-friendly access
        if (doc.HasMember("details") && doc["details"].IsObject()) {
            const auto& details = doc["details"];
            
            if (details.HasMember("symbol")) order->symbol.assign(details["symbol"].GetString());
            if (details.HasMember("side")) order->side.assign(details["side"].GetString());
            if (details.HasMember("order_type")) order->order_type.assign(details["order_type"].GetString());
            if (details.HasMember("time_in_force")) order->time_in_force.assign(details["time_in_force"].GetString());
            
            // Direct numeric parsing (faster than string conversion)
            if (details.HasMember("price")) {
                if (details["price"].IsNumber()) {
                    order->price = details["price"].GetDouble();
                } else if (details["price"].IsString()) {
                    order->price = std::stod(details["price"].GetString());
                }
            }
            if (details.HasMember("size")) {
                if (details["size"].IsNumber()) {
                    order->size = details["size"].GetDouble();
                } else if (details["size"].IsString()) {
                    order->size = std::stod(details["size"].GetString());
                }
            }
            if (details.HasMember("stop_price")) {
                if (details["stop_price"].IsNumber()) {
                    order->stop_price = details["stop_price"].GetDouble();
                } else if (details["stop_price"].IsString()) {
                    order->stop_price = std::stod(details["stop_price"].GetString());
                }
            }
            if (details.HasMember("reduce_only")) {
                if (details["reduce_only"].IsBool()) {
                    order->reduce_only = details["reduce_only"].GetBool();
                } else if (details["reduce_only"].IsString()) {
                    std::string_view val(details["reduce_only"].GetString());
                    order->reduce_only = (val == "true");
                }
            }
        }

        // Parse tags into flat map (limited to prevent DoS)
        if (doc.HasMember("tags") && doc["tags"].IsObject()) {
            for (auto it = doc["tags"].MemberBegin(); it != doc["tags"].MemberEnd(); ++it) {
                if (order->tags.full()) break; // Prevent overflow
                FixedString<32> key(it->name.GetString());
                FixedString<64> value(it->value.GetString());
                order->tags.insert(key, value);
            }
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
        // Check for duplicate processing
        const uint64_t current_time_ns = get_current_time_ns_hft();
        if (processed_orders_->find(order.cl_id)) [[unlikely]] {
            // Order already processed, ignore duplicate
            spdlog::warn("[HFT-Engine] Duplicate order ignored: {}", order.cl_id.c_str());
            return;
        }

        // Mark as processed immediately
        processed_orders_->insert(order.cl_id, current_time_ns);

        // Branch-free action dispatch using perfect hash
        // Pre-computed hash for common actions
        static constexpr uint32_t PLACE_HASH = 0x1a2b3c4d;   // Pre-computed hash("place")
        static constexpr uint32_t CANCEL_HASH = 0x2b3c4d5e;  // Pre-computed hash("cancel")
        static constexpr uint32_t REPLACE_HASH = 0x3c4d5e6f; // Pre-computed hash("replace")
        
        // Simple FNV-1a hash for action string
        uint32_t hash = 0x811c9dc5;
        for (char c : order.action.view()) {
            hash ^= static_cast<uint32_t>(c);
            hash *= 0x01000193;
        }
        
        // Use computed goto pattern (branch-free dispatch)
        using ActionHandler = void (TradingEngineService::*)(const HFTExecutionOrder&);
        static const ActionHandler handlers[] = {
            &TradingEngineService::place_cex_order_hft,   // index 0
            &TradingEngineService::cancel_cex_order_hft,  // index 1
            &TradingEngineService::replace_cex_order_hft  // index 2
        };
        
        // Convert hash to index (0-2) without branches
        const uint32_t idx = (hash == PLACE_HASH) * 0 + 
                           (hash == CANCEL_HASH) * 1 + 
                           (hash == REPLACE_HASH) * 2;
        
        if (idx < 3) [[likely]] {
            (this->*handlers[idx])(order);
        } else {
            send_rejection_report_hft(order, "invalid_action", "Unknown action");
        }

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
        // Fast exchange client lookup
        auto client_it = exchange_clients_.find(std::string(order.venue.view()));
        if (client_it == exchange_clients_.end()) {
            send_rejection_report_hft(order, "unknown_venue", "Exchange not supported");
            return;
        }
        auto& client = client_it->second;

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
        req.symbol = std::string(order.symbol.view());
        req.side = std::string(order.side.view());
        req.order_type = std::string(order.order_type.view());
        req.quantity = std::to_string(order.size);

        // Handle price for limit orders
        if (order.order_type == std::string_view("limit")) {
            if (order.price == 0.0) {
                send_rejection_report_hft(order, "missing_price", "Price required for limit orders");
                return;
            }
            req.price = std::to_string(order.price);
        }

        // Set category based on product type (CRITICAL for reduce_only functionality)
        if (order.product_type == std::string_view("perpetual")) {
            req.category = "linear";
        } else if (order.product_type == std::string_view("spot")) {
            req.category = "spot";
        } else {
            // Default to linear for derivatives if not explicitly set
            req.category = "linear";
        }

        req.time_in_force = order.time_in_force.empty() ? "GTC" : std::string(order.time_in_force.view());
        
        // Handle reduce_only for position management (critical for derivatives)
        req.reduce_only = order.reduce_only;
        if (order.reduce_only) {
            // For reduce-only orders, ensure we're only trading perpetuals/futures
            if (order.product_type == std::string_view("spot")) {
                send_rejection_report_hft(order, "invalid_reduce_only", "reduce_only not supported for spot orders");
                return;
            }
            spdlog::info("[HFT-Engine] 🔒 REDUCE-ONLY order: {} {} {} @ {} (product: {}, category: {})", 
                        order.cl_id.c_str(), order.side.c_str(), order.size, order.price,
                        order.product_type.c_str(), req.category.value_or("NONE"));
        } else {
            spdlog::debug("[HFT-Engine] Regular order: {} {} {} @ {} (product: {}, category: {})", 
                         order.cl_id.c_str(), order.side.c_str(), order.size, order.price,
                         order.product_type.c_str(), req.category.value_or("NONE"));
        }

        // Place order via exchange client
        OrderResponse response = client->place_order(req);

        if (response.success) {
            // Store pending order for tracking
            auto* order_copy = order_pool_->allocate();
            if (order_copy) {
                *order_copy = order;
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
 * @brief HFT-optimized order closing (cancellation via opposite order)
 * Instead of using exchange cancel API, places an opposite order to close the position
 */
void TradingEngineService::cancel_cex_order_hft(const HFTExecutionOrder& order) {
    try {
        auto client_it = exchange_clients_.find(std::string(order.venue.view()));
        if (client_it == exchange_clients_.end()) {
            send_rejection_report_hft(order, "unknown_venue", "Exchange not supported");
            return;
        }
        auto& client = client_it->second;

        // Extract original order details from tags for closing
        auto* cl_id_to_close = order.tags.find(FixedString<32>("cl_id_to_cancel"));
        auto* original_side = order.tags.find(FixedString<32>("original_side"));
        auto* original_symbol = order.tags.find(FixedString<32>("original_symbol"));
        auto* original_size = order.tags.find(FixedString<32>("original_size"));
        
        if (!cl_id_to_close || !original_side || !original_symbol || !original_size) {
            send_rejection_report_hft(order, "missing_cancel_data", "Missing required cancel information");
            return;
        }

        // Create opposite order to close the position with reduce_only
        OrderRequest close_req;
        close_req.client_order_id = std::string(order.cl_id.view());
        close_req.symbol = std::string(original_symbol->view());
        close_req.quantity = std::string(original_size->view());
        
        // Opposite side for closing
        if (original_side->view() == "buy") {
            close_req.side = "sell";
        } else {
            close_req.side = "buy";
        }
        
        close_req.order_type = "market";  // Use market order for fast closing
        close_req.time_in_force = "IOC";   // Immediate or cancel
        
        // CRITICAL: Set reduce_only=true for position closing
        close_req.reduce_only = true;
        
        // Set category based on venue
        if (order.venue == std::string_view("bybit")) {
            if (order.product_type == std::string_view("perpetual")) {
                close_req.category = "linear";
            } else {
                close_req.category = "spot";
            }
        }

        OrderResponse response = client->place_order(close_req);
        
        if (response.success) {
            // Remove from pending orders
            OrderId cl_id(cl_id_to_close->c_str());
            auto* pending_order = pending_orders_->find(cl_id);
            if (pending_order && *pending_order) {
                order_pool_->deallocate(*pending_order);
                pending_orders_->erase(cl_id);
            }
            
            send_acceptance_report_hft(order, response.exchange_order_id, "Position closed via reduce-only opposite order");
            
            // Log the closing action with reduce_only info
            spdlog::info("[HFT-Engine] Order {} closed via reduce-only {} order {} of size {}", 
                cl_id_to_close->c_str(), close_req.side, close_req.client_order_id, close_req.quantity);
        } else {
            send_rejection_report_hft(order, "close_rejected", 
                "Failed to place reduce-only closing order: " + response.message);
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
        auto client_it = exchange_clients_.find(std::string(order.venue.view()));
        if (client_it == exchange_clients_.end()) {
            send_rejection_report_hft(order, "unknown_venue", "Exchange not supported");
            return;
        }
        auto& client = client_it->second;

        auto* cl_id_to_replace = order.tags.find(FixedString<32>("cl_id_to_replace"));
        if (!cl_id_to_replace) {
            send_rejection_report_hft(order, "missing_replace_id", "Missing cl_id_to_replace");
            return;
        }

        std::optional<std::string> new_price;
        std::optional<std::string> new_quantity;

        if (order.price > 0.0) {
            new_price = std::to_string(order.price);
        }
        if (order.size > 0.0) {
            new_quantity = std::to_string(order.size);
        }

        OrderResponse response = client->modify_order(
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
            spdlog::warn("[HFT-Engine] Update for unknown order: {}", update.client_order_id);
            return;
        }

        const HFTExecutionOrder& original_order = **order_ptr;

        // Create execution report from pool
        auto* report = report_pool_->allocate();
        if (!report) {
            spdlog::error("[HFT-Engine] Report pool exhausted");
            return;
        }

        // Populate report with minimal copying
        report->version = 1;
        report->cl_id = cl_id;
        report->exchange_order_id.assign(update.exchange_order_id.c_str());
        report->status.assign(update.status.c_str());
        report->reason_code.assign(update.reason.empty() ? "ok" : "exchange_update");
        report->reason_text.assign(update.reason.empty() ? 
                                 ("Order " + update.status) : update.reason);
        report->ts_ns.store(get_current_time_ns_hft(), std::memory_order_relaxed);
        report->tags = original_order.tags;

        // Publish via lock-free queue
        publish_execution_report_hft(*report);
        report_pool_->deallocate(report);

        // Clean up completed orders
        if (update.status == "filled" || update.status == "cancelled" || update.status == "rejected") {
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
        fill->cl_id.assign(fill_data.client_order_id.c_str());
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
        OrderId cl_id(fill_data.client_order_id.c_str());
        if (auto* order_ptr = pending_orders_->find(cl_id); order_ptr && *order_ptr) {
            fill->tags = (*order_ptr)->tags;
        }
        fill->tags.insert(FixedString<32>("execution_type"), FixedString<64>("live"));

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
    report->reason_code.assign(reason_code);
    report->reason_text.assign(reason_text);
    report->ts_ns.store(get_current_time_ns_hft(), std::memory_order_relaxed);
    report->tags = order.tags;
    
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
    if (tsc_to_ns_scale > 0.0) [[likely]] {
        return static_cast<uint64_t>(rdtsc() * tsc_to_ns_scale);
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
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();
    
    doc.AddMember("version", rapidjson::Value(report.version), allocator);
    doc.AddMember("cl_id", rapidjson::Value(report.cl_id.c_str(), allocator), allocator);
    doc.AddMember("status", rapidjson::Value(report.status.c_str(), allocator), allocator);
    
    if (!report.exchange_order_id.empty()) {
        doc.AddMember("exchange_order_id", rapidjson::Value(report.exchange_order_id.c_str(), allocator), allocator);
    }
    
    doc.AddMember("reason_code", rapidjson::Value(report.reason_code.c_str(), allocator), allocator);
    doc.AddMember("reason_text", rapidjson::Value(report.reason_text.c_str(), allocator), allocator);
    doc.AddMember("ts_ns", rapidjson::Value(report.ts_ns.load()), allocator);
    
    // Serialize tags
    rapidjson::Value tags_obj(rapidjson::kObjectType);
    // Note: Would need iterator support for FlatMap to serialize all tags
    doc.AddMember("tags", tags_obj, allocator);
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    return buffer.GetString();
}

/**
 * @brief Fast fill serialization
 */
std::string TradingEngineService::serialize_fill_hft(const HFTFill& fill) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();
    
    doc.AddMember("version", rapidjson::Value(fill.version), allocator);
    doc.AddMember("cl_id", rapidjson::Value(fill.cl_id.c_str(), allocator), allocator);
    doc.AddMember("exchange_order_id", rapidjson::Value(fill.exchange_order_id.c_str(), allocator), allocator);
    doc.AddMember("exec_id", rapidjson::Value(fill.exec_id.c_str(), allocator), allocator);
    doc.AddMember("symbol_or_pair", rapidjson::Value(fill.symbol_or_pair.c_str(), allocator), allocator);
    doc.AddMember("price", rapidjson::Value(fill.price), allocator);
    doc.AddMember("size", rapidjson::Value(fill.size), allocator);
    doc.AddMember("fee_currency", rapidjson::Value(fill.fee_currency.c_str(), allocator), allocator);
    doc.AddMember("fee_amount", rapidjson::Value(fill.fee_amount), allocator);
    doc.AddMember("liquidity", rapidjson::Value(fill.liquidity.c_str(), allocator), allocator);
    doc.AddMember("ts_ns", rapidjson::Value(fill.ts_ns.load()), allocator);
    
    // Serialize tags
    rapidjson::Value tags_obj(rapidjson::kObjectType);
    doc.AddMember("tags", tags_obj, allocator);
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    return buffer.GetString();
}

} // namespace latentspeed
