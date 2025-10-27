/**
 * @file hyperliquid_web_utils.h
 * @brief Hyperliquid-specific utility functions
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#pragma once

#include <string>
#include <cmath>
#include <stdexcept>

namespace latentspeed::connector::hyperliquid {

/**
 * @class HyperliquidWebUtils
 * @brief Utility functions for Hyperliquid API
 * 
 * Handles float-to-wire precision conversions and rounding
 */
class HyperliquidWebUtils {
public:
    /**
     * @brief Convert float to wire format with specific decimals
     * @param x Value to convert
     * @param decimals Number of decimal places (szDecimals)
     * @return String representation with exact precision
     * 
     * Hyperliquid requires specific precision for sizes:
     * - BTC: 5 decimals (0.00001)
     * - ETH: 4 decimals (0.0001)
     * - Most alts: 3 decimals (0.001)
     */
    static std::string float_to_wire(double x, int decimals) {
        if (std::isnan(x) || std::isinf(x)) {
            throw std::invalid_argument("Cannot convert NaN or Inf to wire format");
        }
        
        // Round to the specified number of decimals
        double multiplier = std::pow(10.0, decimals);
        double rounded = std::round(x * multiplier) / multiplier;
        
        // Format with exact decimal places
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, rounded);
        
        std::string result(buffer);
        
        // Remove trailing zeros after decimal point (but keep at least one)
        if (result.find('.') != std::string::npos) {
            result.erase(result.find_last_not_of('0') + 1, std::string::npos);
            if (result.back() == '.') {
                result += '0';  // Keep at least one decimal
            }
        }
        
        return result;
    }
    
    /**
     * @brief Convert float to integer wire format (scaled by 10^decimals)
     * @param x Value to convert
     * @param decimals Number of decimal places
     * @return Integer representation (x * 10^decimals)
     * 
     * Used for internal calculations where integer arithmetic is needed
     */
    static int64_t float_to_int_wire(double x, int decimals) {
        if (std::isnan(x) || std::isinf(x)) {
            throw std::invalid_argument("Cannot convert NaN or Inf to int wire format");
        }
        
        double multiplier = std::pow(10.0, decimals);
        return static_cast<int64_t>(std::round(x * multiplier));
    }
    
    /**
     * @brief Convert wire format string to float
     * @param wire_str Wire format string
     * @return Parsed float value
     */
    static double wire_to_float(const std::string& wire_str) {
        try {
            return std::stod(wire_str);
        } catch (const std::exception& e) {
            throw std::invalid_argument("Invalid wire format: " + wire_str);
        }
    }
    
    /**
     * @brief Round float to trading precision
     * @param x Value to round
     * @param decimals Number of decimal places
     * @return Rounded value
     */
    static double round_to_decimals(double x, int decimals) {
        double multiplier = std::pow(10.0, decimals);
        return std::round(x * multiplier) / multiplier;
    }
    
    /**
     * @brief Get default size decimals for a symbol
     * @param symbol Trading symbol (e.g., "BTC", "ETH")
     * @return Number of decimals for size
     */
    static int get_default_size_decimals(const std::string& symbol) {
        // Common symbols with known decimals
        if (symbol == "BTC" || symbol == "BTCUSD" || symbol == "BTC-USD") {
            return 5;
        } else if (symbol == "ETH" || symbol == "ETHUSD" || symbol == "ETH-USD") {
            return 4;
        }
        // Default for most altcoins
        return 3;
    }
    
    /**
     * @brief Format price for display
     * @param price Price value
     * @param min_decimals Minimum decimal places
     * @param max_decimals Maximum decimal places
     * @return Formatted price string
     */
    static std::string format_price(double price, int min_decimals = 2, int max_decimals = 8) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.*f", max_decimals, price);
        
        std::string result(buffer);
        
        // Remove trailing zeros but keep minimum decimals
        if (result.find('.') != std::string::npos) {
            size_t decimal_pos = result.find('.');
            size_t last_non_zero = result.find_last_not_of('0');
            
            if (last_non_zero > decimal_pos + min_decimals) {
                result.erase(last_non_zero + 1);
            } else if (last_non_zero <= decimal_pos) {
                result = result.substr(0, decimal_pos + min_decimals + 1);
            } else {
                result = result.substr(0, decimal_pos + min_decimals + 1);
            }
        }
        
        return result;
    }
    
    /**
     * @brief Validate order size meets minimum requirements
     * @param size Order size
     * @param min_size Minimum size
     * @param decimals Size decimals
     * @return true if valid
     */
    static bool validate_size(double size, double min_size, int decimals) {
        if (size < min_size) {
            return false;
        }
        
        // Check if size respects decimal precision
        double multiplier = std::pow(10.0, decimals);
        double scaled = size * multiplier;
        return std::abs(scaled - std::round(scaled)) < 1e-9;
    }
    
    /**
     * @brief Convert notional value to size
     * @param notional Notional value (USD)
     * @param price Price per unit
     * @param decimals Size decimals
     * @return Rounded size
     */
    static double notional_to_size(double notional, double price, int decimals) {
        if (price <= 0.0) {
            throw std::invalid_argument("Price must be positive");
        }
        
        double size = notional / price;
        return round_to_decimals(size, decimals);
    }
};

} // namespace latentspeed::connector::hyperliquid
