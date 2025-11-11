#pragma once

#include <cstdint>
#include <chrono>
#include <cmath>

#ifdef __x86_64__
#include <x86intrin.h>
#endif

namespace latentspeed {
namespace hft {

/**
 * @brief CPU mode for adaptive performance tuning
 */
enum class CpuMode {
    HIGH_PERF,  ///< Maximum performance, aggressive spinning
    NORMAL,     ///< Balanced performance and power
    ECO         ///< Power-saving mode
};

#ifdef __x86_64__
/**
 * @brief Ultra-fast TSC-based timestamp with serializing instruction
 * @return CPU timestamp counter value
 */
[[gnu::hot, gnu::flatten]] inline uint64_t rdtscp() noexcept;

/**
 * @brief Non-serializing TSC read (faster but may reorder)
 */
[[gnu::hot, gnu::flatten]] inline uint64_t rdtsc() noexcept;

/**
 * @brief Calibrate TSC frequency on startup
 */
void calibrate_tsc();

/**
 * @brief Get TSC frequency in Hz
 */
uint64_t get_tsc_frequency();

/**
 * @brief Get TSC to nanoseconds conversion scale
 */
double get_tsc_to_ns_scale();
#endif

/**
 * @brief Get adaptive sleep duration based on CPU mode
 */
std::chrono::nanoseconds get_adaptive_sleep(CpuMode cpu_mode);

/**
 * @brief Memory prefetch hint for cache optimization
 * @param ptr Pointer to prefetch
 * @param locality Temporal locality hint (0-3)
 */
template<typename T>
[[gnu::always_inline]] inline void prefetch(const T* ptr, int locality = 3) noexcept;

/**
 * @brief Compiler fence to prevent reordering
 */
[[gnu::always_inline]] inline void compiler_fence() noexcept;

/**
 * @brief CPU pause instruction for spinlocks
 */
[[gnu::always_inline]] inline void cpu_pause() noexcept;

// ============================================================================
// Inline Implementations
// ============================================================================

#ifdef __x86_64__
[[gnu::hot, gnu::flatten]] inline uint64_t rdtscp() noexcept {
    uint32_t aux;
    return __rdtscp(&aux);
}

[[gnu::hot, gnu::flatten]] inline uint64_t rdtsc() noexcept {
    return __rdtsc();
}
#endif

template<typename T>
[[gnu::always_inline]] inline void prefetch(const T* ptr, int locality) noexcept {
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

[[gnu::always_inline]] inline void compiler_fence() noexcept {
    asm volatile("" ::: "memory");
}

[[gnu::always_inline]] inline void cpu_pause() noexcept {
#ifdef __x86_64__
    __builtin_ia32_pause();
#else
    // Fallback for non-x86 architectures
    std::this_thread::yield();
#endif
}

} // namespace hft
} // namespace latentspeed
