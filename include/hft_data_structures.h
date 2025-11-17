/**
 * @file hft_data_structures.h
 * @brief High-frequency trading optimized data structures using C++20+
 * @author jessiondiwangan@gmail.com
 * @date 2025
 * 
 * Collection of ultra-low-latency data structures optimized for HFT:
 * - Lock-free SPSC/MPSC ring buffers
 * - Memory pools with cache-aligned allocation
 * - Fixed-size string types to avoid dynamic allocation
 * - Cache-friendly flat maps and sets
 * - Atomic synchronization primitives
 */

#pragma once

#include <atomic>
#include <memory>
#include <array>
#include <string_view>
#include <cstring>
#include <new>
#include <concepts>
#include <span>
#include <vector>
#include <algorithm>
#include <bit>

namespace latentspeed::hft {

// ============================================================================
// CACHE-ALIGNED MEMORY AND CONSTANTS
// ============================================================================

static constexpr size_t CACHE_LINE_SIZE = 64;
static constexpr size_t MAX_SYMBOL_LEN = 32;
static constexpr size_t MAX_ORDER_ID_LEN = 64;
static constexpr size_t MAX_MESSAGE_LEN = 2048;

#define CACHE_ALIGNED alignas(CACHE_LINE_SIZE)

// ============================================================================
// FIXED-SIZE STRING TYPES (NO DYNAMIC ALLOCATION)
// ============================================================================

template<size_t N>
class FixedString {
private:
    char data_[N];
    size_t size_;

public:
    constexpr FixedString() noexcept : data_{}, size_(0) {}
    
    constexpr FixedString(std::string_view sv) noexcept : size_(0) {
        assign(sv);
    }
    
    constexpr FixedString(const char* str) noexcept : size_(0) {
        assign(str);
    }
    
    constexpr void assign(std::string_view sv) noexcept {
        size_ = std::min(sv.size(), N - 1);
        std::memcpy(data_, sv.data(), size_);
        data_[size_] = '\0';
    }
    
    constexpr void assign(const char* str) noexcept {
        if (str) {
            assign(std::string_view(str));
        } else {
            clear();
        }
    }
    
    constexpr void clear() noexcept {
        size_ = 0;
        data_[0] = '\0';
    }
    
    [[nodiscard]] constexpr const char* c_str() const noexcept { return data_; }
    [[nodiscard]] constexpr const char* data() const noexcept { return data_; }
    [[nodiscard]] constexpr size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr std::string_view view() const noexcept { return {data_, size_}; }
    
    constexpr bool operator==(const FixedString& other) const noexcept {
        return view() == other.view();
    }
    
    constexpr bool operator==(std::string_view sv) const noexcept {
        return view() == sv;
    }
    
    constexpr bool operator<(const FixedString& other) const noexcept {
        return view() < other.view();
    }
};

using Symbol = FixedString<MAX_SYMBOL_LEN>;
using OrderId = FixedString<MAX_ORDER_ID_LEN>;

// ============================================================================
// LOCK-FREE SPSC RING BUFFER (SINGLE PRODUCER, SINGLE CONSUMER)
// ============================================================================

template<typename T, size_t Capacity>
requires std::is_trivially_copyable_v<T>
class CACHE_ALIGNED LockFreeSPSCQueue {
private:
    static_assert(std::has_single_bit(Capacity), "Capacity must be power of 2 for efficient modulo");
    
    static constexpr size_t MASK = Capacity - 1;
    
    CACHE_ALIGNED std::atomic<size_t> head_{0};  // Consumer index
    CACHE_ALIGNED std::atomic<size_t> tail_{0};  // Producer index
    CACHE_ALIGNED std::array<T, Capacity> buffer_;

public:
    [[nodiscard]] bool try_push(const T& item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & MASK;
        
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue is full
        }
        
        buffer_[current_tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }
    
    [[nodiscard]] bool try_pop(T& item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false; // Queue is empty
        }
        
        item = buffer_[current_head];
        head_.store((current_head + 1) & MASK, std::memory_order_release);
        return true;
    }
    
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }
    
    [[nodiscard]] size_t size() const noexcept {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return (t - h) & MASK;
    }
};

// ============================================================================
// MEMORY POOL ALLOCATOR (PRE-ALLOCATED, CACHE-ALIGNED)
// ============================================================================

template<typename T, size_t PoolSize>
class CACHE_ALIGNED MemoryPool {
private:
    struct FreeNode {
        FreeNode* next;
    };
    
    CACHE_ALIGNED std::array<std::aligned_storage_t<sizeof(T), alignof(T)>, PoolSize> pool_;
    CACHE_ALIGNED std::atomic<FreeNode*> free_head_;
    // Track available count to avoid racing traversal in available()
    CACHE_ALIGNED std::atomic<size_t> free_count_;
    // Lightweight allocation state to guard against double-free (0 = allocated, 1 = free)
    std::array<std::atomic<uint8_t>, PoolSize> state_;

    inline size_t index_of_(void* p) const noexcept {
        auto base = reinterpret_cast<const unsigned char*>(&pool_[0]);
        auto cur = reinterpret_cast<const unsigned char*>(p);
        size_t off = static_cast<size_t>(cur - base);
        return off / sizeof(pool_[0]);
    }
    
public:
    MemoryPool() noexcept {
        // Initialize free list
        for (size_t i = 0; i < PoolSize - 1; ++i) {
            auto* node = reinterpret_cast<FreeNode*>(&pool_[i]);
            node->next = reinterpret_cast<FreeNode*>(&pool_[i + 1]);
        }
        auto* last_node = reinterpret_cast<FreeNode*>(&pool_[PoolSize - 1]);
        last_node->next = nullptr;
        
        free_head_.store(reinterpret_cast<FreeNode*>(&pool_[0]), std::memory_order_relaxed);
        free_count_.store(PoolSize, std::memory_order_relaxed);
        for (size_t i = 0; i < PoolSize; ++i) {
            state_[i].store(1, std::memory_order_relaxed); // 1 = free
        }
    }
    
    template<typename... Args>
    [[nodiscard]] T* allocate(Args&&... args) noexcept {
        FreeNode* node = free_head_.load(std::memory_order_acquire);
        
        while (node != nullptr) {
            if (free_head_.compare_exchange_weak(node, node->next, 
                                               std::memory_order_release, 
                                               std::memory_order_acquire)) {
                // Mark allocated and adjust count
                size_t idx = index_of_(node);
                state_[idx].store(0, std::memory_order_release);
                free_count_.fetch_sub(1, std::memory_order_acq_rel);
                return new(node) T(std::forward<Args>(args)...);
            }
        }
        
        return nullptr; // Pool exhausted
    }
    
    void deallocate(T* ptr) noexcept {
        if (ptr == nullptr) return;
        
        // Guard against double-free: only free if we transition from allocated(0) -> free(1)
        size_t idx = index_of_(ptr);
        uint8_t expected = 0;
        if (!state_[idx].compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
            // Already free; ignore
            return;
        }

        ptr->~T();

        auto* node = reinterpret_cast<FreeNode*>(ptr);
        node->next = free_head_.load(std::memory_order_acquire);
        while (!free_head_.compare_exchange_weak(node->next, node,
                                               std::memory_order_release,
                                               std::memory_order_acquire)) {
            // Retry
        }
        free_count_.fetch_add(1, std::memory_order_acq_rel);
    }
    
    [[nodiscard]] size_t available() const noexcept {
        return free_count_.load(std::memory_order_acquire);
    }
};

// ============================================================================
// CACHE-FRIENDLY FLAT MAP (SORTED VECTOR, BETTER CACHE LOCALITY)
// ============================================================================

template<typename Key, typename Value, size_t MaxSize>
class FlatMap {
private:
    struct Entry {
        Key key;
        Value value;
        
        constexpr bool operator<(const Entry& other) const noexcept {
            return key < other.key;
        }
    };
    
    std::array<Entry, MaxSize> data_;
    size_t size_{0};

public:
    constexpr FlatMap() noexcept = default;
    
    [[nodiscard]] constexpr Value* find(const Key& key) noexcept {
        auto it = std::lower_bound(data_.begin(), data_.begin() + size_, 
                                 Entry{key, Value{}});
        if (it != data_.begin() + size_ && it->key == key) {
            return &it->value;
        }
        return nullptr;
    }
    
    [[nodiscard]] constexpr const Value* find(const Key& key) const noexcept {
        auto it = std::lower_bound(data_.begin(), data_.begin() + size_, 
                                 Entry{key, Value{}});
        if (it != data_.begin() + size_ && it->key == key) {
            return &it->value;
        }
        return nullptr;
    }
    
    constexpr bool insert(const Key& key, const Value& value) noexcept {
        if (size_ >= MaxSize) return false;
        
        auto it = std::lower_bound(data_.begin(), data_.begin() + size_, 
                                 Entry{key, Value{}});
        
        if (it != data_.begin() + size_ && it->key == key) {
            it->value = value; // Update existing
            return true;
        }
        
        // Insert new entry
        std::move_backward(it, data_.begin() + size_, data_.begin() + size_ + 1);
        *it = Entry{key, value};
        ++size_;
        return true;
    }
    
    constexpr bool erase(const Key& key) noexcept {
        auto it = std::lower_bound(data_.begin(), data_.begin() + size_, 
                                 Entry{key, Value{}});
        if (it != data_.begin() + size_ && it->key == key) {
            std::move(it + 1, data_.begin() + size_, it);
            --size_;
            return true;
        }
        return false;
    }
    
    [[nodiscard]] constexpr size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr bool full() const noexcept { return size_ >= MaxSize; }
    
    constexpr void clear() noexcept { size_ = 0; }

    template <typename Fn>
    constexpr void for_each(Fn&& fn) noexcept {
        for (size_t i = 0; i < size_; ++i) {
            fn(data_[i].key, data_[i].value);
        }
    }

    template <typename Fn>
    constexpr void for_each(Fn&& fn) const noexcept {
        for (size_t i = 0; i < size_; ++i) {
            fn(data_[i].key, data_[i].value);
        }
    }
};

// ============================================================================
// HIGH-PERFORMANCE ORDER STRUCTURES
// ============================================================================

struct CACHE_ALIGNED HFTExecutionOrder {
    int32_t version{1};
    OrderId cl_id;
    FixedString<16> action;         // "place", "cancel", "replace"
    FixedString<16> venue_type;     // "cex", "dex", "amm"
    FixedString<16> venue;          // "bybit", "binance", etc.
    FixedString<16> product_type;   // "spot", "perpetual", "option"
    std::atomic<uint64_t> ts_ns;
    
    // Order details stored in fixed-size arrays for cache efficiency
    Symbol symbol;
    FixedString<8> side;           // "buy", "sell"
    FixedString<16> order_type;    // "market", "limit", "stop"
    FixedString<8> time_in_force;  // "GTC", "IOC", "FOK"
    
    double price{0.0};
    double size{0.0};
    double stop_price{0.0};
    bool reduce_only{false};
    
    // Fast tag lookup (max 8 tags)
    FlatMap<FixedString<32>, FixedString<64>, 8> tags;
    FlatMap<FixedString<32>, FixedString<64>, 12> params;
    
    constexpr HFTExecutionOrder() noexcept = default;
    
    // Copy constructor optimized for cache efficiency
    HFTExecutionOrder(const HFTExecutionOrder& other) noexcept
        : version(other.version)
        , cl_id(other.cl_id)
        , action(other.action)
        , venue_type(other.venue_type)
        , venue(other.venue)
        , product_type(other.product_type)
        , ts_ns(other.ts_ns.load())
        , symbol(other.symbol)
        , side(other.side)
        , order_type(other.order_type)
        , time_in_force(other.time_in_force)
        , price(other.price)
        , size(other.size)
        , stop_price(other.stop_price)
        , reduce_only(other.reduce_only)
        , tags(other.tags)
        , params(other.params) {
    }

    HFTExecutionOrder& operator=(const HFTExecutionOrder& other) noexcept {
        if (this != &other) {
            version = other.version;
            cl_id = other.cl_id;
            action = other.action;
            venue_type = other.venue_type;
            venue = other.venue;
            product_type = other.product_type;
            ts_ns.store(other.ts_ns.load());
            symbol = other.symbol;
            side = other.side;
            order_type = other.order_type;
            time_in_force = other.time_in_force;
            price = other.price;
            size = other.size;
            stop_price = other.stop_price;
            reduce_only = other.reduce_only;
            tags = other.tags;
            params = other.params;
        }
        return *this;
    }
};

struct CACHE_ALIGNED HFTExecutionReport {
    int32_t version{1};
    OrderId cl_id;
    OrderId exchange_order_id;
    FixedString<16> status;        // "accepted", "rejected", "filled", etc.
    FixedString<32> reason_code;
    FixedString<128> reason_text;
    std::atomic<uint64_t> ts_ns;
    
    // Fast tag lookup
    FlatMap<FixedString<32>, FixedString<64>, 8> tags;
    
    constexpr HFTExecutionReport() noexcept = default;
};

struct CACHE_ALIGNED HFTFill {
    int32_t version{1};
    OrderId cl_id;
    OrderId exchange_order_id;
    OrderId exec_id;
    Symbol symbol_or_pair;
    FixedString<8> side;           // "buy", "sell" - REQUIRED for balance accounting

    double price{0.0};
    double size{0.0};
    double fee_amount{0.0};
    FixedString<8> fee_currency;
    FixedString<8> liquidity;      // "maker", "taker"
    std::atomic<uint64_t> ts_ns;

    // Fast tag lookup
    FlatMap<FixedString<32>, FixedString<64>, 8> tags;

    constexpr HFTFill() noexcept = default;
};

// ============================================================================
// MESSAGE TYPES FOR LOCK-FREE QUEUES
// ============================================================================

enum class MessageType : uint8_t {
    EXECUTION_REPORT = 1,
    FILL = 2,
    ERROR = 3
};

struct CACHE_ALIGNED PublishMessage {
    MessageType type;
    FixedString<16> topic;
    FixedString<MAX_MESSAGE_LEN> payload;
    uint64_t timestamp_ns;  // Changed from atomic to regular uint64_t for SPSC queue compatibility
    
    constexpr PublishMessage() noexcept 
        : type(MessageType::EXECUTION_REPORT), timestamp_ns(0) {}
    
    constexpr PublishMessage(MessageType t, std::string_view topic_str, std::string_view payload_str) noexcept
        : type(t), timestamp_ns(0) {
        topic.assign(topic_str);
        payload.assign(payload_str);
    }
    
    // Default copy/move constructors and assignment operators are now sufficient
    constexpr PublishMessage(const PublishMessage&) noexcept = default;
    constexpr PublishMessage& operator=(const PublishMessage&) noexcept = default;
    constexpr PublishMessage(PublishMessage&&) noexcept = default;
    constexpr PublishMessage& operator=(PublishMessage&&) noexcept = default;
};

// ============================================================================
// ATOMIC STATISTICS COUNTERS (FOR PERFORMANCE MONITORING)
// ============================================================================

struct CACHE_ALIGNED HFTStats {
    std::atomic<uint64_t> orders_received{0};
    std::atomic<uint64_t> orders_processed{0};
    std::atomic<uint64_t> orders_rejected{0};
    std::atomic<uint64_t> fills_received{0};
    std::atomic<uint64_t> messages_published{0};
    std::atomic<uint64_t> queue_full_count{0};
    std::atomic<uint64_t> memory_pool_exhausted{0};
    
    // Latency tracking (nanoseconds)
    std::atomic<uint64_t> min_processing_latency_ns{UINT64_MAX};
    std::atomic<uint64_t> max_processing_latency_ns{0};
    std::atomic<uint64_t> total_processing_latency_ns{0};
    
    void update_latency(uint64_t latency_ns) noexcept {
        // Update min
        uint64_t current_min = min_processing_latency_ns.load();
        while (latency_ns < current_min && 
               !min_processing_latency_ns.compare_exchange_weak(current_min, latency_ns)) {
            // Retry
        }
        
        // Update max
        uint64_t current_max = max_processing_latency_ns.load();
        while (latency_ns > current_max && 
               !max_processing_latency_ns.compare_exchange_weak(current_max, latency_ns)) {
            // Retry
        }
        
        // Update total
        total_processing_latency_ns.fetch_add(latency_ns, std::memory_order_relaxed);
    }
    
    [[nodiscard]] double get_average_latency_ns() const noexcept {
        uint64_t total = total_processing_latency_ns.load();
        uint64_t count = orders_processed.load();
        return count > 0 ? static_cast<double>(total) / count : 0.0;
    }
};

} // namespace latentspeed::hft
