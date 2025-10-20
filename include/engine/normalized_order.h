/**
 * @file normalized_order.h
 * @brief Phase 2: Normalized DTOs for venue-agnostic order data.
 */

#pragma once

#include <string>
#include <optional>
#include <map>

namespace latentspeed {

struct NormalizedSymbol {
    std::string base;    // e.g., "ETH"
    std::string quote;   // e.g., "USDT"
    std::string settle;  // e.g., "USDT" (empty for spot)
    enum class Kind { spot, perp, option } kind{Kind::spot};
};

struct NormalizedOrder {
    NormalizedSymbol sym;
    std::string side;     // "buy" | "sell"
    std::string type;     // "limit" | "market" | "stop" | "stop_limit"
    std::string tif;      // "gtc" | "ioc" | "fok" | "post_only"
    double size{0.0};
    std::optional<double> price;
    std::optional<double> stop_price;
    bool reduce_only{false};
    std::map<std::string,std::string> params;
};

} // namespace latentspeed

