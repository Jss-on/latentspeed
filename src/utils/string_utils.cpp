#include "utils/string_utils.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <array>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace latentspeed {
namespace utils {

std::string to_lower_ascii(std::string_view input) {
    std::string out(input.begin(), input.end());
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string to_upper_ascii(std::string_view input) {
    std::string out(input.begin(), input.end());
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

std::string trim_trailing_zeros(std::string value) {
    if (auto pos = value.find('.'); pos != std::string::npos) {
        auto last = value.find_last_not_of('0');
        if (last != std::string::npos) {
            value.erase(last + 1);
        }
        if (!value.empty() && value.back() == '.') {
            value.pop_back();
        }
    }
    if (value.empty()) {
        return std::string{"0"};
    }
    return value;
}

std::string format_decimal(double value, int precision) {
    if (!std::isfinite(value)) {
        return std::string{"0"};
    }
    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss << std::setprecision(precision) << value;
    return trim_trailing_zeros(oss.str());
}

std::string normalize_venue_key(std::string_view venue) {
    return to_lower_ascii(venue);
}

std::optional<std::pair<std::string,std::string>> split_compact_symbol(std::string_view sym) {
    static const std::array<const char*, 8> quotes = {
        "USDT","USDC","BTC","ETH","USD","EUR","DAI","FDUSD"
    };
    std::string s(sym.begin(), sym.end());
    for (auto q : quotes) {
        const size_t qlen = std::strlen(q);
        if (s.size() > qlen && s.rfind(q) == s.size() - qlen) {
            return std::make_pair(s.substr(0, s.size() - qlen), std::string(q));
        }
    }
    return std::nullopt;
}

std::string to_hyphen_symbol(std::string_view symbol, bool is_perp) {
    std::string s(symbol.begin(), symbol.end());
    if (s.empty()) return s;

    // If ccxt perp style: BASE/QUOTE:SETTLE
    if (auto colon = s.find(':'); colon != std::string::npos) {
        std::string left = s.substr(0, colon);
        if (auto slash = left.find('/'); slash != std::string::npos) {
            std::string base = left.substr(0, slash);
            std::string quote = left.substr(slash+1);
            return base + "-" + quote + (is_perp ? "-PERP" : "");
        }
    }
    // If ccxt spot style: BASE/QUOTE
    if (auto slash = s.find('/'); slash != std::string::npos) {
        std::string base = s.substr(0, slash);
        std::string quote = s.substr(slash+1);
        return base + "-" + quote + (is_perp ? "-PERP" : "");
    }
    // If already hyphenated
    if (s.find('-') != std::string::npos) {
        // Ensure PERP suffix if requested
        if (is_perp && s.rfind("-PERP") != s.size() - 5) {
            return s + "-PERP";
        }
        return s;
    }
    // Compact form like BNBUSDT
    if (auto parts = split_compact_symbol(s)) {
        return parts->first + std::string("-") + parts->second + (is_perp ? "-PERP" : "");
    }
    return s;
}

std::string normalize_symbol_compact(std::string_view symbol, [[maybe_unused]] std::string_view product_type) {
    std::string s(symbol.begin(), symbol.end());
    if (s.empty()) {
        return s;
    }

    // Remove settle suffix if present (e.g., ETH/USDT:USDT)
    if (auto colon = s.find(':'); colon != std::string::npos) {
        s = s.substr(0, colon);
    }

    std::string upper = to_upper_ascii(s);
    const std::string perp_suffix = "-PERP";
    if (upper.size() > perp_suffix.size()) {
        auto tail = upper.substr(upper.size() - perp_suffix.size());
        if (tail == perp_suffix) {
            upper.erase(upper.size() - perp_suffix.size());
        }
    }

    std::string compact;
    compact.reserve(upper.size());
    for (char c : upper) {
        if (c == '-' || c == '/') {
            continue;
        }
        compact.push_back(c);
    }

    // Fallback: if product_type signals spot but compact is empty, return original upper
    if (compact.empty()) {
        return upper;
    }
    return compact;
}

std::optional<std::string> map_time_in_force(std::string_view tif_raw) {
    if (tif_raw.empty()) {
        return std::nullopt;
    }
    auto upper = to_upper_ascii(tif_raw);
    if (upper == "GTC") {
        return std::string{"GTC"};
    }
    if (upper == "IOC") {
        return std::string{"IOC"};
    }
    if (upper == "FOK") {
        return std::string{"FOK"};
    }
    if (upper == "PO" || upper == "POST_ONLY") {
        return std::string{"PostOnly"};
    }
    return std::string(tif_raw);
}

std::optional<std::string> normalize_report_status(std::string_view raw_status) {
    auto lower = to_lower_ascii(raw_status);
    if (lower == "new" || lower == "partiallyfilled" || lower == "filled" || lower == "accepted") {
        return std::string{"accepted"};
    }
    if (lower == "cancelled" || lower == "canceled" || lower == "partiallyfilledcancelled" || lower == "inactive" || lower == "deactivated") {
        return std::string{"canceled"};
    }
    if (lower == "rejected") {
        return std::string{"rejected"};
    }
    if (lower == "amended" || lower == "replaced") {
        return std::string{"replaced"};
    }
    return std::nullopt;
}

std::string normalize_reason_code(std::string_view normalized_status, std::string_view raw_reason) {
    if (normalized_status == "rejected") {
        std::string lower_reason = to_lower_ascii(raw_reason);
        if (lower_reason.find("balance") != std::string::npos) {
            return std::string{"insufficient_balance"};
        }
        return std::string{"venue_reject"};
    }
    if (normalized_status == "canceled") {
        return std::string{"ok"};
    }
    if (normalized_status == "replaced") {
        return std::string{"ok"};
    }
    return std::string{"ok"};
}

std::string build_reason_text(std::string_view normalized_status, std::string_view raw_reason) {
    if (!raw_reason.empty()) {
        if (raw_reason == "EC_NoError") {
            return std::string{"OK"};
        }
        return std::string(raw_reason);
    }
    if (normalized_status == "canceled") {
        return std::string{"Order cancelled"};
    }
    if (normalized_status == "rejected") {
        return std::string{"Order rejected"};
    }
    if (normalized_status == "replaced") {
        return std::string{"Order replaced"};
    }
    return std::string{"OK"};
}

bool is_terminal_status(std::string_view raw_status) {
    auto lower = to_lower_ascii(raw_status);
    return lower == "filled" || lower == "cancelled" || lower == "canceled" || lower == "rejected" || lower == "partiallyfilledcancelled";
}

} // namespace utils
} // namespace latentspeed
