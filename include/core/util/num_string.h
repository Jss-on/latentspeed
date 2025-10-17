/**
 * @file num_string.h
 * @brief Helpers for canonical numeric string formatting required by some venues.
 */

#pragma once

#include <string>
#include <string_view>
#include <algorithm>

namespace latentspeed::util {

inline std::string trim_trailing_zeros(std::string value) {
    auto dot = value.find('.');
    if (dot != std::string::npos) {
        auto last = value.find_last_not_of('0');
        if (last != std::string::npos) {
            value.erase(last + 1);
        }
        if (!value.empty() && value.back() == '.') {
            value.pop_back();
        }
    }
    if (value.empty()) return std::string{"0"};
    return value;
}

inline std::string to_lower_ascii(std::string_view s) {
    std::string out(s.begin(), s.end());
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c){
        if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
        return static_cast<char>(c);
    });
    return out;
}

inline std::string to_lower_hex_address(std::string_view s) {
    std::string out = to_lower_ascii(s);
    if (out.rfind("0x", 0) != 0) {
        out.insert(out.begin(), {'0','x'});
    }
    return out;
}

} // namespace latentspeed::util

