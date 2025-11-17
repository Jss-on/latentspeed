/**
 * @file hyperliquid_asset_resolver.h
 * @brief Hyperliquid asset resolver: maps coin/pairs to asset IDs using /info meta endpoints.
 */

#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>
#include <chrono>

#include "adapters/hyperliquid_config.h"

namespace latentspeed::nethttp { class HttpClient; }

namespace latentspeed {

struct HlResolution {
    int asset{-1};            // Perps: index in meta.universe; Spot: 10000 + spot index
    int sz_decimals{-1};      // Size decimals if known; -1 if unknown
};

/**
 * @class HyperliquidAssetResolver
 * @brief Caches and resolves perps and spot assets for Hyperliquid.
 *
 * Fetches `/info` payloads for `meta` and `spotMeta`, caches results with TTL,
 * and supports mapping:
 *  - Perps: coin name (e.g., "BTC") -> asset index, szDecimals
 *  - Spot: pair (e.g., "PURR/USDC") -> asset id = 10000 + index
 */
class HyperliquidAssetResolver {
public:
    explicit HyperliquidAssetResolver(HyperliquidConfig cfg);

    // Force refresh meta/spot caches now; returns true if both succeed
    bool refresh_all();

    // Resolve perps coin -> asset index and szDecimals
    std::optional<HlResolution> resolve_perp(std::string_view coin);

    // Resolve spot pair (BASE/QUOTE) -> asset id (10000 + index). szDecimals may be unknown.
    std::optional<HlResolution> resolve_spot(std::string_view base, std::string_view quote);

    // Resolve spot pair name by index (for WS coin like "@107"). Returns {base, quote} if available.
    std::optional<std::pair<std::string,std::string>> resolve_spot_pair_by_index(int index);

    // Resolve perp coin name by asset index (inverse lookup of resolve_perp)
    // Returns the coin string (e.g., "BTC") if known.
    std::optional<std::string> resolve_perp_coin_by_index(int index);

    // Configure cache TTL
    void set_ttl(std::chrono::seconds ttl) { ttl_ = ttl; }

private:
    // Lazy refresh helpers
    bool ensure_meta();
    bool ensure_spot_meta();

    // HTTP POST /info {"type": <t>}
    std::optional<std::string> post_info(const std::string& type);

    // Parsing helpers
    bool parse_perp_meta_json(const std::string& json);
    bool parse_spot_meta_json(const std::string& json);

    // URL pieces from cfg.rest_base
    std::string scheme_;
    std::string host_;
    std::string port_;

    HyperliquidConfig cfg_;

    // Caches
    std::unordered_map<std::string, HlResolution> perp_coin_to_res_;
    std::unordered_map<std::string, int> token_name_to_id_;  // e.g., USDC -> 0
    std::unordered_map<int, std::string> token_id_to_name_;
    // spot universe: pair index -> [baseTokenId, quoteTokenId]
    std::unordered_map<int, std::pair<int,int>> spot_index_to_tokens_;

    std::chrono::steady_clock::time_point meta_time_{};
    std::chrono::steady_clock::time_point spot_meta_time_{};
    std::chrono::seconds ttl_{std::chrono::seconds(300)}; // 5 minutes default
};

} // namespace latentspeed
