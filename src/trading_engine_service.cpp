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

namespace {

std::string to_lower_ascii(std::string_view input) {
    std::string out(input.begin(), input.end());
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string to_upper_ascii(std::string_view input) {
    std::string out(input.begin(), input.end());
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

std::string trim_trailing_zeros(std::string value) {
    if (auto pos = value.find('.'); pos != std::string::npos) {
        auto last = value.find_last_not_of('0');
        if (last != std::string::npos) {
            value.erase(last + 1);
        }
        if (!value.empty() && value.back() == '.') {
            value.pop_back();
        }
    }
    if (value.empty()) {
        return std::string{"0"};
    }
    return value;
}

std::string format_decimal(double value, int precision = 12) {
    if (!std::isfinite(value)) {
        return std::string{"0"};
    }
    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss << std::setprecision(precision) << value;
    return trim_trailing_zeros(oss.str());
}

std::string normalize_symbol_for_bybit(std::string_view symbol, [[maybe_unused]] std::string_view product_type) {
    std::string s(symbol.begin(), symbol.end());
    if (s.empty()) {
        return s;
    }

    // Remove settle suffix if present (e.g., ETH/USDT:USDT)
    if (auto colon = s.find(':'); colon != std::string::npos) {
        s = s.substr(0, colon);
    }

    std::string upper = to_upper_ascii(s);
    const std::string perp_suffix = "-PERP";
    if (upper.size() > perp_suffix.size()) {
        auto tail = upper.substr(upper.size() - perp_suffix.size());
        if (tail == perp_suffix) {
            upper.erase(upper.size() - perp_suffix.size());
        }
    }

    std::string compact;
    compact.reserve(upper.size());
    for (char c : upper) {
        if (c == '-' || c == '/') {
            continue;
        }
        compact.push_back(c);
    }

    // Fallback: if product_type signals spot but compact is empty, return original upper
    if (compact.empty()) {
        return upper;
    }
    return compact;
}

std::optional<std::string> map_time_in_force(std::string_view tif_raw) {
    if (tif_raw.empty()) {
        return std::nullopt;
    }
    auto upper = to_upper_ascii(tif_raw);
    if (upper == "GTC") {
        return std::string{"GTC"};
    }
    if (upper == "IOC") {
        return std::string{"IOC"};
    }
    if (upper == "FOK") {
        return std::string{"FOK"};
    }
    if (upper == "PO" || upper == "POST_ONLY") {
        return std::string{"PostOnly"};
    }
    return std::string(tif_raw);
}

std::optional<std::string> normalize_report_status(std::string_view raw_status) {
    auto lower = to_lower_ascii(raw_status);
    if (lower == "new" || lower == "partiallyfilled" || lower == "filled" || lower == "accepted") {
        return std::string{"accepted"};
    }
    if (lower == "cancelled" || lower == "canceled" || lower == "partiallyfilledcancelled" || lower == "inactive" || lower == "deactivated") {
        return std::string{"canceled"};
    }
    if (lower == "rejected") {
        return std::string{"rejected"};
    }
    if (lower == "amended" || lower == "replaced") {
        return std::string{"replaced"};
    }
    return std::nullopt;
}

std::string normalize_reason_code(std::string_view normalized_status, std::string_view raw_reason) {
    if (normalized_status == "rejected") {
        std::string lower_reason = to_lower_ascii(raw_reason);
        if (lower_reason.find("balance") != std::string::npos) {
            return std::string{"insufficient_balance"};
        }
        return std::string{"venue_reject"};
    }
    if (normalized_status == "canceled") {
        return std::string{"ok"};
    }
    if (normalized_status == "replaced") {
        return std::string{"ok"};
    }
    return std::string{"ok"};
}

std::string build_reason_text(std::string_view normalized_status, std::string_view raw_reason) {
    if (!raw_reason.empty()) {
        if (raw_reason == "EC_NoError") {
            return std::string{"OK"};
        }
        return std::string(raw_reason);
    }
    if (normalized_status == "canceled") {
        return std::string{"Order cancelled"};
    }
    if (normalized_status == "rejected") {
        return std::string{"Order rejected"};
    }
    if (normalized_status == "replaced") {
        return std::string{"Order replaced"};
    }
    return std::string{"OK"};
}

bool is_terminal_status(std::string_view raw_status) {
    auto lower = to_lower_ascii(raw_status);
    return lower == "filled" || lower == "cancelled" || lower == "canceled" || lower == "rejected" || lower == "partiallyfilledcancelled";
}

} // namespace


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
    if (ptr == nullptr) {
        return;
    }

    switch (locality) {
        case 0:
            __builtin_prefetch(ptr, 0, 0);
            break;
        case 1:
            __builtin_prefetch(ptr, 0, 1);
            break;
        case 2:
            __builtin_prefetch(ptr, 0, 2);
            break;
        case 3:
            __builtin_prefetch(ptr, 0, 3);
            break;
        default:
            __builtin_prefetch(ptr, 0, 3);
            break;
    }
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
TradingEngineService::TradingEngineService(CpuMode cpu_mode)
    : running_(false)
    , order_endpoint_("tcp://127.0.0.1:5601")
    , report_endpoint_("tcp://127.0.0.1:5602")
{
    // Handle CPU mode configuration
    (void)cpu_mode; // Mark as used to avoid warning
    
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
        
        // Create Bybit client - for testing, we'll use a minimal setup
        auto bybit_client = std::make_unique<BybitClient>();
        
        auto getenv_string = [](const char* key) -> std::string {
            const char* value = std::getenv(key);
            return value ? std::string(value) : std::string();
        };

        auto getenv_bool = [](const char* key, bool default_value) -> bool {
            const char* value = std::getenv(key);
            if (!value) {
                return default_value;
            }
            std::string lower = to_lower_ascii(value);
            if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
                return true;
            }
            if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
                return false;
            }
            return default_value;
        };

        std::string api_key = getenv_string("LATENTSPEED_BYBIT_API_KEY");
        std::string api_secret = getenv_string("LATENTSPEED_BYBIT_API_SECRET");
        bool use_testnet = getenv_bool("LATENTSPEED_BYBIT_USE_TESTNET", true);

        if (api_key.empty() || api_secret.empty()) {
            spdlog::error("[HFT-Engine] Missing Bybit credentials. Set LATENTSPEED_BYBIT_API_KEY and LATENTSPEED_BYBIT_API_SECRET.");
            return false;
        }
        
        // For cancel validation testing, we can initialize without connecting to exchange
        // This allows us to test the execution report flow
        if (!bybit_client->initialize(api_key, api_secret, use_testnet)) {
            spdlog::error("[HFT-Engine] Failed to initialize Bybit client with provided credentials");
            return false;
        }

        // Set callbacks for order updates and fills
        bybit_client->set_order_update_callback([this](const OrderUpdate& update) {
            this->on_order_update_hft(update);
        });
        
        bybit_client->set_fill_callback([this](const FillData& fill) {
            this->on_fill_hft(fill);
        });
        
        if (!bybit_client->connect()) {
            spdlog::warn("[HFT-Engine] Bybit WebSocket not connected; fills may be delayed");
        }
        
        // Add to exchange clients map - this is the key fix!
        exchange_clients_["bybit"] = std::move(bybit_client);
        {
            auto& client = *exchange_clients_["bybit"];

            struct Req { std::string category; std::optional<std::string> settle; };
            std::vector<Req> batches = {
                {"linear", std::string("USDT")},
                {"linear", std::string("USDC")},
                {"inverse", std::nullopt},      // many are coin-settled; we’ll rely on lazy rehydrate if API needs baseCoin
                {"spot",    std::nullopt},      // some Bybit routes require baseCoin/symbol; try best-effort
            };

            size_t inserted_total = 0;

            for (const auto& b : batches) {
                std::optional<std::string> base_coin;  // none for now (no extra configs)
                std::optional<std::string> symbol;     // none for now

                auto briefs = client.list_open_orders(b.category, symbol, b.settle, base_coin);
                size_t inserted = 0;

                for (const auto& x : briefs) {
                    OrderId key(x.client_order_id.c_str());
                    if (auto* p = pending_orders_->find(key); p && *p) continue;

                    auto* o = order_pool_->allocate();
                    if (!o) continue;
                    new (o) HFTExecutionOrder();

                    o->version = 1;
                    o->cl_id.assign(x.client_order_id.c_str());
                    o->venue.assign("bybit");
                    o->venue_type.assign("cex");
                    o->product_type.assign((x.category == "spot") ? "spot" : "perpetual");
                    o->symbol.assign(x.symbol.c_str());
                    o->side.assign(x.side.c_str());
                    o->order_type.assign(x.order_type.c_str());
                    if (!x.qty.empty()) {
                        try { o->size = std::stod(x.qty); } catch (...) { o->size = 0.0; }
                    }
                    o->reduce_only = x.reduce_only;

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


        spdlog::info("[HFT-Engine] Exchange clients initialized: bybit (use_testnet={})", use_testnet ? "true" : "false");
        
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
    
    spdlog::info("[HFT-Engine] Ultra-low latency service started");
    spdlog::info("[HFT-Engine] Real-time scheduling enabled");
    spdlog::info("[HFT-Engine] CPU affinity configured");
}

void TradingEngineService::stop() {
    if (!running_.exchange(false)) {
        return;
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

            auto parse_object_to_params = [&](const rapidjson::Value& obj, const std::string& prefix = std::string()) {
                for (auto it = obj.MemberBegin(); it != obj.MemberEnd(); ++it) {
                    if (order->params.full()) break;
                    std::string key = prefix.empty() ? it->name.GetString()
                                                      : prefix + it->name.GetString();
                    if (it->value.IsString()) {
                        order->params.insert(FixedString<32>(key.c_str()), FixedString<64>(it->value.GetString()));
                    } else if (it->value.IsNumber()) {
                        std::string str_val = format_decimal(it->value.GetDouble());
                        order->params.insert(FixedString<32>(key.c_str()), FixedString<64>(str_val.c_str()));
                    } else if (it->value.IsBool()) {
                        const char* bool_val = it->value.GetBool() ? "true" : "false";
                        order->params.insert(FixedString<32>(key.c_str()), FixedString<64>(bool_val));
                    }
                }
            };

            if (details.HasMember("params") && details["params"].IsObject()) {
                parse_object_to_params(details["params"]);
            }

            if (details.HasMember("cancel") && details["cancel"].IsObject()) {
                parse_object_to_params(details["cancel"], "cancel_");
            }

            if (details.HasMember("replace") && details["replace"].IsObject()) {
                parse_object_to_params(details["replace"], "replace_");
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

        // Normalise symbol for Bybit requirements and ensure lowercase side/order type
        const std::string product_type_lower = to_lower_ascii(order.product_type.view());
        const std::string category = product_type_lower.empty() || product_type_lower == "spot" ? "spot" : "linear";
        req.category = category;
        req.symbol = normalize_symbol_for_bybit(order.symbol.view(), order.product_type.view());
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
 * @brief HFT-optimized native cancellation flow
 */
void TradingEngineService::cancel_cex_order_hft(const HFTExecutionOrder& order) {
    try {
        auto client_it = exchange_clients_.find(std::string(order.venue.view()));
        if (client_it == exchange_clients_.end()) {
            send_rejection_report_hft(order, "unknown_venue", "Exchange not supported");
            return;
        }
        auto& client = client_it->second;

        auto find_value = [&](const char* key) -> const FixedString<64>* {
            if (auto* param = order.params.find(FixedString<32>(key))) {
                return param;
            }
            return order.tags.find(FixedString<32>(key));
        };

        const auto* cl_id_to_cancel = find_value("cancel_cl_id_to_cancel");
        if (!cl_id_to_cancel) {
            send_rejection_report_hft(order, "missing_cancel_id", "Missing cl_id_to_cancel for cancel request");
            return;
        }

        std::optional<std::string> exchange_order_id;
        if (auto* exch = find_value("cancel_exchange_order_id")) {
            exchange_order_id = std::string(exch->c_str());
        }

        const std::string cl_to_cancel_str = cl_id_to_cancel->c_str();

        std::optional<std::string> symbol;
        if (!order.symbol.empty()) {
            symbol = normalize_symbol_for_bybit(order.symbol.view(), order.product_type.view());
        }

        // Fallback to cached pending order metadata when not supplied in request
        OrderId original_id_lookup(cl_to_cancel_str.c_str());
        if (!symbol.has_value()) {
            if (auto* pending_order = pending_orders_->find(original_id_lookup); pending_order && *pending_order) {
                symbol = normalize_symbol_for_bybit((*pending_order)->symbol.view(), (*pending_order)->product_type.view());
            }
        }
        if (!exchange_order_id.has_value()) {
            if (auto* pending_order = pending_orders_->find(original_id_lookup); pending_order && *pending_order) {
                if (auto* stored_exch = (*pending_order)->params.find(FixedString<32>("exchange_order_id"))) {
                    exchange_order_id = std::string(stored_exch->c_str());
                }
            }
        }

        OrderResponse response = client->cancel_order(cl_to_cancel_str, symbol, exchange_order_id);

        auto to_lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){return static_cast<char>(std::tolower(c));});
            return s;
        };

        const std::string msg_lower = to_lower(response.message);
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
        auto client_it = exchange_clients_.find(std::string(order.venue.view()));
        if (client_it == exchange_clients_.end()) {
            send_rejection_report_hft(order, "unknown_venue", "Exchange not supported");
            return;
        }
        auto& client = client_it->second;

        auto find_value = [&](const char* key) -> const FixedString<64>* {
            if (auto* param = order.params.find(FixedString<32>(key))) {
                return param;
            }
            return order.tags.find(FixedString<32>(key));
        };

        const auto* cl_id_to_replace = find_value("replace_cl_id_to_replace");
        if (!cl_id_to_replace) {
            send_rejection_report_hft(order, "missing_replace_id", "Missing cl_id_to_replace");
            return;
        }

        std::optional<std::string> new_price;
        std::optional<std::string> new_quantity;

        if (auto* price_override = find_value("replace_new_price")) {
            if (!price_override->empty()) {
                new_price = price_override->c_str();
            }
        } else if (order.price > 0.0) {
            new_price = format_decimal(order.price);
        }

        if (auto* size_override = find_value("replace_new_size")) {
            if (!size_override->empty()) {
                new_quantity = size_override->c_str();
            }
        } else if (order.size > 0.0) {
            new_quantity = format_decimal(order.size);
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
            // Unknown order
            if (is_terminal_status(update.status)) {
                // Late terminal update: idempotent; safe to ignore
                spdlog::info("[HFT-Engine] Late terminal update for non-tracked order: {} status={}",
                             update.client_order_id, update.status);
                return;
            }

            // Non-terminal unknown update → try lazy rehydration from exchange
            ExchangeClient* client_raw = nullptr;
            if (exchange_clients_.size() == 1) {
                // single venue configured
                client_raw = exchange_clients_.begin()->second.get();
            } else {
                auto it = exchange_clients_.find(std::string("bybit"));
                if (it != exchange_clients_.end()) client_raw = it->second.get();
            }

            if (client_raw) {
                OrderResponse qr = client_raw->query_order(update.client_order_id);
                if (qr.success) {
                    // Expect symbol/category in extra_data (added in BybitClient earlier)
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
                            o->venue.assign("bybit");          // we queried bybit
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

        std::string reason_code = normalize_reason_code(*normalized_status, update.reason);
        std::string reason_text = build_reason_text(*normalized_status, update.reason);
        report->reason_code.assign(reason_code.c_str());
        report->reason_text.assign(reason_text.c_str());
        report->ts_ns.store(get_current_time_ns_hft(), std::memory_order_relaxed);
        report->tags = original_order.tags; // placeholder will have empty tags, which is fine

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
        if (auto* order_ptr = pending_orders_->find(lookup_id); order_ptr && *order_ptr) {
            fill->tags = (*order_ptr)->tags;
            if (!(*order_ptr)->symbol.empty()) {
                fill->symbol_or_pair.assign((*order_ptr)->symbol.c_str());
            }
            fill->tags.insert(FixedString<32>("execution_type"), FixedString<64>("live"));
        } else {
            // External/manual action: still publish with clear tagging
            fill->tags.insert(FixedString<32>("execution_type"), FixedString<64>("external"));
        }

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
    const std::string canonical = latentspeed::exec::canonical_reason_code(reason_code);
    report->reason_code.assign(canonical.c_str());
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
    report.tags.for_each([&](const auto& key, const auto& value) {
        rapidjson::Value json_key(key.c_str(), allocator);
        rapidjson::Value json_val(value.c_str(), allocator);
        tags_obj.AddMember(json_key, json_val, allocator);
    });
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
    fill.tags.for_each([&](const auto& key, const auto& value) {
        rapidjson::Value json_key(key.c_str(), allocator);
        rapidjson::Value json_val(value.c_str(), allocator);
        tags_obj.AddMember(json_key, json_val, allocator);
    });
    doc.AddMember("tags", tags_obj, allocator);
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    return buffer.GetString();
}

} // namespace latentspeed
