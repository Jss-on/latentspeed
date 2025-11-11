#include "utils/hft_utils.h"
#include <spdlog/spdlog.h>
#include <thread>

namespace latentspeed {
namespace hft {

#ifdef __x86_64__
// Calibration constants for TSC to nanoseconds conversion
static thread_local uint64_t tsc_frequency = 0;
static thread_local double tsc_to_ns_scale = 0.0;

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
    
    spdlog::info("[HFT-Utils] TSC frequency calibrated: {} Hz", tsc_frequency);
}

uint64_t get_tsc_frequency() {
    return tsc_frequency;
}

double get_tsc_to_ns_scale() {
    return tsc_to_ns_scale;
}
#endif

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

} // namespace hft
} // namespace latentspeed
