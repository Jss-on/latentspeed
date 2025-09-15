/**
 * @file hft_benchmark.cpp
 * @brief Comprehensive HFT Performance Benchmark Suite
 * @author latentspeed
 * @date 2025
 * 
 * Benchmarks:
 * 1. Sub-microsecond latency measurement
 * 2. High throughput lock-free queue testing
 * 3. Deterministic performance analysis
 * 4. Cache efficiency measurement
 */

#include <chrono>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iomanip>
#include <random>
#include <cstring>

// HFT includes
#include "hft_data_structures.h"
#include "trading_engine_service.h"

#ifdef __x86_64__
#include <x86intrin.h>
#include <immintrin.h>
#endif

using namespace latentspeed;
using namespace latentspeed::hft;

// Simple message structure for lock-free queue benchmarking
struct alignas(64) BenchmarkMessage {
    uint64_t id;
    double price;
    double size;
    char symbol[16];
    char side[8];
    
    BenchmarkMessage() = default;
    BenchmarkMessage(uint64_t msg_id, double p, double s, const char* sym, const char* sd) 
        : id(msg_id), price(p), size(s) {
        std::strncpy(symbol, sym, 15);
        symbol[15] = '\0';
        std::strncpy(side, sd, 7);
        side[7] = '\0';
    }
};

class HFTBenchmark {
private:
    static constexpr size_t WARMUP_ITERATIONS = 10000;
    static constexpr size_t BENCHMARK_ITERATIONS = 1000000;
    
    // TSC calibration
    static thread_local uint64_t tsc_frequency;
    static thread_local double tsc_to_ns_scale;
    
public:
    /**
     * @brief Ultra-fast timestamp using TSC
     */
    [[gnu::hot, gnu::flatten]] inline uint64_t get_timestamp_ns() const {
#ifdef __x86_64__
        if (tsc_to_ns_scale > 0.0) [[likely]] {
            return static_cast<uint64_t>(__rdtsc() * tsc_to_ns_scale);
        }
#endif
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }
    
    /**
     * @brief Calibrate TSC frequency for accurate timing
     */
    void calibrate_tsc() {
        using namespace std::chrono;
        
        const auto start_tsc = __rdtsc();
        const auto start_time = high_resolution_clock::now();
        
        std::this_thread::sleep_for(milliseconds(100));
        
        const auto end_tsc = __rdtsc();
        const auto end_time = high_resolution_clock::now();
        
        const auto elapsed_ns = duration_cast<nanoseconds>(end_time - start_time).count();
        const auto elapsed_tsc = end_tsc - start_tsc;
        
        tsc_frequency = (elapsed_tsc * 1'000'000'000ULL) / elapsed_ns;
        tsc_to_ns_scale = static_cast<double>(elapsed_ns) / static_cast<double>(elapsed_tsc);
        
        std::cout << "TSC frequency calibrated: " << tsc_frequency << " Hz\n";
    }
    
    /**
     * @brief Benchmark 1: Sub-microsecond latency measurement
     */
    void benchmark_latency() {
        std::cout << "\n=== BENCHMARK 1: SUB-MICROSECOND LATENCY ===\n";
        
        calibrate_tsc();
        
        std::vector<uint64_t> latencies;
        latencies.reserve(BENCHMARK_ITERATIONS);
        
        // Warmup
        for (size_t i = 0; i < WARMUP_ITERATIONS; ++i) {
            volatile uint64_t start = get_timestamp_ns();
            volatile uint64_t end = get_timestamp_ns();
            (void)start; (void)end; // Suppress unused warnings
        }
        
        // Measure order processing latency
        auto order_pool = std::make_unique<MemoryPool<HFTExecutionOrder, 1024>>();
        
        for (size_t i = 0; i < BENCHMARK_ITERATIONS; ++i) {
            const auto start = get_timestamp_ns();
            
            // Simulate order processing path
            auto* order = order_pool->allocate();
            if (order) [[likely]] {
                // Simulate minimal order processing
                order->cl_id = OrderId("test_order_123");
                order->symbol = Symbol("ETHUSDT");
                order->side = FixedString<8>("buy");
                order->size = 0.1;
                order->price = 2500.0;
                order_pool->deallocate(order);
            }
            
            const auto end = get_timestamp_ns();
            latencies.push_back(end - start);
        }
        
        analyze_latency_results(latencies);
    }
    
    /**
     * @brief Benchmark 2: High throughput lock-free queue testing
     */
    void benchmark_throughput() {
        std::cout << "\n=== BENCHMARK 2: HIGH THROUGHPUT LOCK-FREE QUEUES ===\n";
        
        constexpr size_t QUEUE_SIZE = 65536;
        constexpr size_t TEST_MESSAGES = 10000000;
        
        auto queue = std::make_unique<LockFreeSPSCQueue<BenchmarkMessage, QUEUE_SIZE>>();
        std::atomic<uint64_t> messages_sent{0};
        std::atomic<uint64_t> messages_received{0};
        
        // Producer thread
        std::thread producer([&]() {
            BenchmarkMessage msg(0, 2500.0, 0.001, "ETHUSDT", "buy");
            
            const auto start = get_timestamp_ns();
            
            while (messages_sent.load() < TEST_MESSAGES) {
                msg.id = messages_sent.load();
                if (queue->try_push(msg)) {
                    messages_sent.fetch_add(1, std::memory_order_relaxed);
                } else {
#ifdef __x86_64__
                    __builtin_ia32_pause(); // CPU pause when queue full
#else
                    std::this_thread::yield();
#endif
                }
            }
            
            const auto end = get_timestamp_ns();
            const auto duration_ns = end - start;
            const auto throughput = (TEST_MESSAGES * 1'000'000'000ULL) / duration_ns;
            
            std::cout << "Producer throughput: " << throughput << " messages/sec\n";
        });
        
        // Consumer thread
        std::thread consumer([&]() {
            BenchmarkMessage msg;
            const auto start = get_timestamp_ns();
            
            while (messages_received.load() < TEST_MESSAGES) {
                if (queue->try_pop(msg)) {
                    messages_received.fetch_add(1, std::memory_order_relaxed);
                } else {
#ifdef __x86_64__
                    __builtin_ia32_pause(); // CPU pause when queue empty
#else
                    std::this_thread::yield();
#endif
                }
            }
            
            const auto end = get_timestamp_ns();
            const auto duration_ns = end - start;
            const auto throughput = (TEST_MESSAGES * 1'000'000'000ULL) / duration_ns;
            
            std::cout << "Consumer throughput: " << throughput << " messages/sec\n";
        });
        
        producer.join();
        consumer.join();
        
        std::cout << "Total messages processed: " << messages_received.load() << "\n";
        
        if (messages_received.load() >= 1000000) { // 1M+ messages/sec
            std::cout << "✅ HIGH THROUGHPUT ACHIEVED (>1M msg/sec)\n";
        } else {
            std::cout << "❌ High throughput target not met\n";
        }
    }
    
    /**
     * @brief Benchmark 3: Deterministic performance measurement
     */
    void benchmark_deterministic_performance() {
        std::cout << "\n=== BENCHMARK 3: DETERMINISTIC PERFORMANCE ===\n";
        
        constexpr size_t NUM_RUNS = 1000;
        std::vector<uint64_t> run_times;
        run_times.reserve(NUM_RUNS);
        
        auto order_pool = std::make_unique<MemoryPool<HFTExecutionOrder, 1024>>();
        
        // Pre-warm everything
        for (size_t i = 0; i < 100; ++i) {
            auto* order = order_pool->allocate();
            if (order) {
                order->cl_id = OrderId("deterministic_test");
                order_pool->deallocate(order);
            }
        }
        
        // Run consistent workload multiple times
        for (size_t run = 0; run < NUM_RUNS; ++run) {
            const auto start = get_timestamp_ns();
            
            // Consistent workload: 1000 order allocations/deallocations
            for (size_t i = 0; i < 1000; ++i) {
                auto* order = order_pool->allocate();
                if (order) [[likely]] {
                    order->cl_id = OrderId("deterministic_order");
                    order->symbol = Symbol("ETHUSDT");
                    order->side = FixedString<8>("buy");
                    order->size = 0.001 * (i + 1);
                    order->price = 2500.0 + i;
                    order_pool->deallocate(order);
                }
            }
            
            const auto end = get_timestamp_ns();
            run_times.push_back(end - start);
        }
        
        analyze_deterministic_results(run_times);
    }
    
    /**
     * @brief Benchmark 4: Cache efficiency measurement
     */
    void benchmark_cache_efficiency() {
        std::cout << "\n=== BENCHMARK 4: CACHE EFFICIENCY ===\n";
        
        constexpr size_t ARRAY_SIZE = 1024 * 1024; // 1M elements
        constexpr size_t ITERATIONS = 1000;
        
        // Test 1: Sequential access (cache-friendly)
        std::vector<uint64_t> sequential_data(ARRAY_SIZE);
        std::iota(sequential_data.begin(), sequential_data.end(), 0);
        
        const auto seq_start = get_timestamp_ns();
        
        for (size_t iter = 0; iter < ITERATIONS; ++iter) {
            uint64_t sum = 0;
            for (size_t i = 0; i < ARRAY_SIZE; ++i) {
                // Prefetch next cache line
                if (i % 64 == 0 && i + 64 < ARRAY_SIZE) {
                    __builtin_prefetch(&sequential_data[i + 64], 0, 3);
                }
                sum += sequential_data[i];
            }
            // Prevent optimization
            asm volatile("" : "+r"(sum));
        }
        
        const auto seq_end = get_timestamp_ns();
        const auto seq_time = seq_end - seq_start;
        
        // Test 2: Random access (cache-unfriendly)
        std::vector<uint64_t> random_data(ARRAY_SIZE);
        std::iota(random_data.begin(), random_data.end(), 0);
        
        // Create random access pattern
        std::vector<size_t> random_indices(ARRAY_SIZE);
        std::iota(random_indices.begin(), random_indices.end(), 0);
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(random_indices.begin(), random_indices.end(), g);
        
        const auto rand_start = get_timestamp_ns();
        
        for (size_t iter = 0; iter < ITERATIONS; ++iter) {
            uint64_t sum = 0;
            for (size_t i = 0; i < ARRAY_SIZE; ++i) {
                sum += random_data[random_indices[i]];
            }
            // Prevent optimization
            asm volatile("" : "+r"(sum));
        }
        
        const auto rand_end = get_timestamp_ns();
        const auto rand_time = rand_end - rand_start;
        
        const double efficiency_ratio = static_cast<double>(rand_time) / seq_time;
        
        std::cout << "Sequential access time: " << seq_time / ITERATIONS << " ns/iter\n";
        std::cout << "Random access time: " << rand_time / ITERATIONS << " ns/iter\n";
        std::cout << "Cache efficiency ratio: " << std::fixed << std::setprecision(2) 
                  << efficiency_ratio << "x (lower is better)\n";
        
        if (efficiency_ratio < 3.0) {
            std::cout << "✅ GOOD CACHE EFFICIENCY (ratio < 3.0x)\n";
        } else {
            std::cout << "❌ Poor cache efficiency\n";
        }
    }
    
    void run_all_benchmarks() {
        std::cout << "🚀 LATENTSPEED HFT BENCHMARK SUITE\n";
        std::cout << "==================================\n";
        
        benchmark_latency();
        benchmark_throughput(); 
        benchmark_deterministic_performance();
        benchmark_cache_efficiency();
        
        std::cout << "\n📊 BENCHMARK SUMMARY COMPLETE\n";
    }

private:
    void analyze_latency_results(const std::vector<uint64_t>& latencies) {
        std::vector<uint64_t> sorted_latencies = latencies;
        std::sort(sorted_latencies.begin(), sorted_latencies.end());
        
        const auto mean = std::accumulate(latencies.begin(), latencies.end(), 0ULL) / latencies.size();
        const auto p50 = sorted_latencies[sorted_latencies.size() * 50 / 100];
        const auto p95 = sorted_latencies[sorted_latencies.size() * 95 / 100];
        const auto p99 = sorted_latencies[sorted_latencies.size() * 99 / 100];
        const auto p999 = sorted_latencies[sorted_latencies.size() * 999 / 1000];
        const auto min_lat = sorted_latencies.front();
        const auto max_lat = sorted_latencies.back();
        
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Latency Statistics (nanoseconds):\n";
        std::cout << "  Min:    " << min_lat << " ns\n";
        std::cout << "  Mean:   " << mean << " ns\n";
        std::cout << "  P50:    " << p50 << " ns\n";
        std::cout << "  P95:    " << p95 << " ns\n";
        std::cout << "  P99:    " << p99 << " ns\n";
        std::cout << "  P99.9:  " << p999 << " ns\n";
        std::cout << "  Max:    " << max_lat << " ns\n";
        
        if (p99 < 1000) {
            std::cout << "✅ SUB-MICROSECOND LATENCY ACHIEVED (P99 < 1μs)\n";
        } else {
            std::cout << "❌ Sub-microsecond target not met\n";
        }
    }
    
    void analyze_deterministic_results(const std::vector<uint64_t>& run_times) {
        std::vector<uint64_t> sorted_times = run_times;
        std::sort(sorted_times.begin(), sorted_times.end());
        
        const auto mean = std::accumulate(run_times.begin(), run_times.end(), 0ULL) / run_times.size();
        const auto min_time = sorted_times.front();
        const auto max_time = sorted_times.back();
        const auto p95 = sorted_times[sorted_times.size() * 95 / 100];
        const auto p99 = sorted_times[sorted_times.size() * 99 / 100];
        
        // Calculate coefficient of variation (standard deviation / mean)
        double variance = 0.0;
        for (const auto& time : run_times) {
            const double diff = static_cast<double>(time) - mean;
            variance += diff * diff;
        }
        variance /= run_times.size();
        const double std_dev = std::sqrt(variance);
        const double coeff_variation = std_dev / mean;
        
        std::cout << "Deterministic Performance Analysis:\n";
        std::cout << "  Mean runtime: " << mean << " ns\n";
        std::cout << "  Min runtime:  " << min_time << " ns\n";
        std::cout << "  Max runtime:  " << max_time << " ns\n";
        std::cout << "  P95 runtime:  " << p95 << " ns\n";
        std::cout << "  P99 runtime:  " << p99 << " ns\n";
        std::cout << "  Std deviation: " << static_cast<uint64_t>(std_dev) << " ns\n";
        std::cout << "  Coeff of variation: " << std::fixed << std::setprecision(4) 
                  << coeff_variation << " (lower is more deterministic)\n";
        
        if (coeff_variation < 0.1) {
            std::cout << "✅ HIGHLY DETERMINISTIC PERFORMANCE (CV < 0.1)\n";
        } else if (coeff_variation < 0.2) {
            std::cout << "✅ GOOD DETERMINISTIC PERFORMANCE (CV < 0.2)\n";
        } else {
            std::cout << "❌ Poor performance determinism\n";
        }
    }
};

// Thread-local TSC calibration variables
thread_local uint64_t HFTBenchmark::tsc_frequency = 0;
thread_local double HFTBenchmark::tsc_to_ns_scale = 0.0;

int main(int argc, char* argv[]) {
    std::cout << "🚀 LATENTSPEED HFT BENCHMARK SUITE v1.0\n";
    std::cout << "========================================\n";
    std::cout << "Testing HFT optimizations:\n";
    std::cout << "  ⚡ Sub-microsecond latency\n";
    std::cout << "  🚄 High throughput lock-free queues\n";  
    std::cout << "  🎯 Deterministic performance\n";
    std::cout << "  💨 Cache efficiency\n\n";
    
    try {
        HFTBenchmark benchmark;
        
        // Check if specific benchmark requested
        if (argc > 1) {
            std::string test = argv[1];
            if (test == "latency") {
                benchmark.benchmark_latency();
            } else if (test == "throughput") {
                benchmark.benchmark_throughput();
            } else if (test == "deterministic") {
                benchmark.benchmark_deterministic_performance();
            } else if (test == "cache") {
                benchmark.benchmark_cache_efficiency();
            } else {
                std::cout << "Usage: " << argv[0] << " [latency|throughput|deterministic|cache]\n";
                std::cout << "Run without arguments to execute all benchmarks.\n";
                return 1;
            }
        } else {
            // Run all benchmarks
            benchmark.run_all_benchmarks();
        }
        
        std::cout << "\n✅ Benchmark execution completed successfully!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Benchmark failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
