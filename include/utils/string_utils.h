#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <utility>

namespace latentspeed {
namespace utils {

/**
 * @brief Convert string to lowercase ASCII
 */
std::string to_lower_ascii(std::string_view input);

/**
 * @brief Convert string to uppercase ASCII
 */
std::string to_upper_ascii(std::string_view input);

/**
 * @brief Trim trailing zeros from decimal string
 */
std::string trim_trailing_zeros(std::string value);

/**
 * @brief Format decimal number to string with trimming
 * @param value Numeric value to format
 * @param precision Decimal precision (default: 12)
 */
std::string format_decimal(double value, int precision = 12);

/**
 * @brief Normalize venue name to lowercase key
 */
std::string normalize_venue_key(std::string_view venue);

/**
 * @brief Split compact symbol like BNBUSDT into (BNB, USDT)
 * @return Optional pair of (base, quote) currencies
 */
std::optional<std::pair<std::string,std::string>> split_compact_symbol(std::string_view sym);

/**
 * @brief Convert symbol to hyphen style: BASE-QUOTE or BASE-QUOTE-PERP
 */
std::string to_hyphen_symbol(std::string_view symbol, bool is_perp);

/**
 * @brief Normalize symbol to compact form (removes hyphens, slashes)
 */
std::string normalize_symbol_compact(std::string_view symbol, std::string_view product_type);

/**
 * @brief Map time-in-force string to standard format
 */
std::optional<std::string> map_time_in_force(std::string_view tif_raw);

/**
 * @brief Normalize order status to standard values
 * @return "accepted", "canceled", "rejected", "replaced", or nullopt
 */
std::optional<std::string> normalize_report_status(std::string_view raw_status);

/**
 * @brief Map normalized status and raw reason to reason code
 */
std::string normalize_reason_code(std::string_view normalized_status, std::string_view raw_reason);

/**
 * @brief Build human-readable reason text from status and raw reason
 */
std::string build_reason_text(std::string_view normalized_status, std::string_view raw_reason);

/**
 * @brief Check if order status is terminal (filled, cancelled, rejected)
 */
bool is_terminal_status(std::string_view raw_status);

} // namespace utils
} // namespace latentspeed
