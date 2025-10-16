/**
 * @file symbol_mapper.h
 * @brief Phase 2: Symbol normalization helpers.
 */

#pragma once

#include <string>
#include <string_view>

namespace latentspeed {

class ISymbolMapper {
public:
    virtual ~ISymbolMapper() = default;
    // Convert external/ccxt/hyphen to compact (e.g., "ETH/USDT:USDT" or "ETH-USDT" -> "ETHUSDT")
    virtual std::string to_compact(std::string_view symbol, std::string_view product_type) const = 0;
    // Convert venue symbol (compact or ccxt) to hyphen normalized (e.g., "ETHUSDT" -> "ETH-USDT", PERP -> "-PERP")
    virtual std::string to_hyphen(std::string_view symbol, bool is_perp) const = 0;
};

class DefaultSymbolMapper : public ISymbolMapper {
public:
    std::string to_compact(std::string_view symbol, std::string_view product_type) const override;
    std::string to_hyphen(std::string_view symbol, bool is_perp) const override;
};

} // namespace latentspeed

