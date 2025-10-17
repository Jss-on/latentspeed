/**
 * @file symbol_mapper.cpp
 * @brief Default symbol normalization implementation.
 */

#include "core/symbol/symbol_mapper.h"
#include <array>
#include <cstring>
#include <string>

namespace latentspeed {

static inline std::string to_upper_ascii(std::string_view input) {
    std::string out(input.begin(), input.end());
    for (auto& c : out) { if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A'); }
    return out;
}

static inline std::string_view trim_settle_suffix(std::string_view s) {
    auto colon = s.find(':');
    if (colon != std::string_view::npos) return s.substr(0, colon);
    return s;
}

static inline std::string compact_from_hyphen_or_slash(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '-' || c == '/') continue;
        out.push_back(c);
    }
    return out;
}

static inline std::string hyphen_from_slash_or_hyphen(std::string_view s, bool is_perp) {
    std::string str(s.begin(), s.end());
    if (str.empty()) return str;
    // ccxt perp form: BASE/QUOTE:SETTLE
    if (auto colon = str.find(':'); colon != std::string::npos) {
        std::string left = str.substr(0, colon);
        if (auto slash = left.find('/'); slash != std::string::npos) {
            std::string base = left.substr(0, slash);
            std::string quote = left.substr(slash+1);
            return base + "-" + quote + (is_perp ? "-PERP" : "");
        }
    }
    // If already hyphenated
    if (str.find('-') != std::string::npos) {
        if (is_perp && (str.rfind("-PERP") != str.size() - 5)) return str + "-PERP";
        return str;
    }
    // Otherwise assume compact and try to split via common quote set
    static const std::array<const char*, 8> quotes = {"USDT","USDC","BTC","ETH","USD","EUR","DAI","FDUSD"};
    std::string upper = to_upper_ascii(str);
    for (auto q : quotes) {
        const size_t qlen = std::strlen(q);
        if (upper.size() > qlen && upper.rfind(q) == upper.size() - qlen) {
            return upper.substr(0, upper.size() - qlen) + std::string("-") + q + (is_perp ? "-PERP" : "");
        }
    }
    return upper + (is_perp ? "-PERP" : "");
}

std::string DefaultSymbolMapper::to_compact(std::string_view symbol, std::string_view product_type) const {
    // Remove settle suffix if present (e.g., ETH/USDT:USDT)
    auto s = trim_settle_suffix(symbol);
    std::string upper = to_upper_ascii(s);
    // Drop a trailing -PERP suffix from hyphen form when building compact
    const std::string perp_suffix = "-PERP";
    if (upper.size() > perp_suffix.size()) {
        auto tail = upper.substr(upper.size() - perp_suffix.size());
        if (tail == perp_suffix) {
            upper.erase(upper.size() - perp_suffix.size());
        }
    }
    return compact_from_hyphen_or_slash(upper);
}

std::string DefaultSymbolMapper::to_hyphen(std::string_view symbol, bool is_perp) const {
    return hyphen_from_slash_or_hyphen(symbol, is_perp);
}

} // namespace latentspeed

