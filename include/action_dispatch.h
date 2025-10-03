#pragma once

#include <cstdint>
#include <string_view>

namespace latentspeed::dispatch {

/**
 * @brief Lightweight FNV-1a (32-bit) hash suitable for compile-time evaluation.
 */
constexpr std::uint32_t fnv1a_32(std::string_view text) noexcept {
    std::uint32_t hash = 0x811C9DC5u;
    for (unsigned char ch : text) {
        hash ^= static_cast<std::uint32_t>(ch);
        hash *= 0x01000193u;
    }
    return hash;
}

enum class ActionKind : std::uint8_t {
    Place = 0,
    Cancel = 1,
    Replace = 2,
    Unknown = 255,
};

constexpr std::uint32_t kPlaceHash = fnv1a_32("place");
constexpr std::uint32_t kCancelHash = fnv1a_32("cancel");
constexpr std::uint32_t kReplaceHash = fnv1a_32("replace");

static_assert(kPlaceHash == 0xC8D632FCu, "FNV-1a hash mismatch for 'place'");
static_assert(kCancelHash == 0x066E9C1Bu, "FNV-1a hash mismatch for 'cancel'");
static_assert(kReplaceHash == 0xA13884C3u, "FNV-1a hash mismatch for 'replace'");

/**
 * @brief Decode an action string to a strongly-typed value using FNV-1a hashing.
 *
 * The caller is expected to normalize the input (e.g., lowercase) before invoking
 * this helper. Unknown actions fall back to ActionKind::Unknown.
 */
constexpr ActionKind decode_action(std::string_view normalized_action) noexcept {
    const std::uint32_t hash = fnv1a_32(normalized_action);
    if (hash == kPlaceHash) {
        return ActionKind::Place;
    }
    if (hash == kCancelHash) {
        return ActionKind::Cancel;
    }
    if (hash == kReplaceHash) {
        return ActionKind::Replace;
    }
    return ActionKind::Unknown;
}

}  // namespace latentspeed::dispatch
