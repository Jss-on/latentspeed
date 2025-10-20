/**
 * @file hyperliquid_nonce.h
 * @brief Per-signer atomic millisecond nonce manager for Hyperliquid actions.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace latentspeed {

class HyperliquidNonceManager {
public:
    HyperliquidNonceManager() : last_nonce_ms_(0) {}

    // Returns a strictly monotonic nonce in milliseconds.
    // Ensures nonce >= now_ms, and > last returned.
    uint64_t next() {
        uint64_t now = now_ms();
        uint64_t prev = last_nonce_ms_.load(std::memory_order_relaxed);
        uint64_t candidate = (now > prev) ? now : (prev + 1);
        while (!last_nonce_ms_.compare_exchange_weak(prev, candidate,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_relaxed)) {
            candidate = (now > prev) ? now : (prev + 1);
        }
        return candidate;
    }

    // Fast-forward the counter to at least now_ms if it has drifted behind.
    void fast_forward_to_now() {
        uint64_t now = now_ms();
        uint64_t prev = last_nonce_ms_.load(std::memory_order_relaxed);
        while (prev < now && !last_nonce_ms_.compare_exchange_weak(prev, now,
                                                                    std::memory_order_acq_rel,
                                                                    std::memory_order_relaxed)) {
            // loop
        }
    }

    // Reset (use sparingly; e.g., after process restart). Safe since we enforce >= now_ms in next().
    void reset() { last_nonce_ms_.store(0, std::memory_order_release); }

    uint64_t last() const { return last_nonce_ms_.load(std::memory_order_acquire); }

private:
    static uint64_t now_ms() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    std::atomic<uint64_t> last_nonce_ms_;
};

} // namespace latentspeed

