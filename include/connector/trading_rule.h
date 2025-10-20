/**
 * @file trading_rule.h
 * @brief Trading rules and constraints for a trading pair
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#pragma once

#include <string>
#include <optional>

namespace latentspeed::connector {

/**
 * @struct TradingRule
 * @brief Trading rules and constraints for a specific trading pair
 * 
 * Contains exchange-specific rules like minimum order size,
 * price precision, tick size, etc.
 */
struct TradingRule {
    std::string trading_pair;
    
    // Price constraints
    double min_price = 0.0;
    double max_price = 0.0;
    double tick_size = 0.0;              // Minimum price increment
    int price_decimals = 8;              // Number of decimal places for price
    
    // Size/quantity constraints
    double min_order_size = 0.0;
    double max_order_size = 0.0;
    double min_notional = 0.0;           // Minimum order value (price * size)
    double step_size = 0.0;              // Minimum size increment
    int size_decimals = 8;               // Number of decimal places for size
    
    // Additional constraints
    std::optional<int> max_num_orders;   // Maximum open orders per pair
    std::optional<int> max_num_algo_orders;
    
    // Exchange-specific
    bool supports_limit_maker = true;
    bool supports_post_only = true;
    bool supports_market_orders = true;
    bool supports_stop_orders = false;
    
    /**
     * @brief Check if trading is enabled for this pair
     */
    bool is_trading_enabled() const {
        return min_order_size > 0.0 && tick_size > 0.0;
    }
    
    /**
     * @brief Quantize price according to tick size and decimals
     */
    double quantize_price(double price) const {
        if (tick_size <= 0.0) return price;
        
        // Round to nearest tick
        double ticks = std::round(price / tick_size);
        double quantized = ticks * tick_size;
        
        // Round to price decimals
        double multiplier = std::pow(10.0, price_decimals);
        return std::round(quantized * multiplier) / multiplier;
    }
    
    /**
     * @brief Quantize size according to step size and decimals
     */
    double quantize_size(double size) const {
        if (step_size <= 0.0) return size;
        
        // Round to nearest step
        double steps = std::round(size / step_size);
        double quantized = steps * step_size;
        
        // Round to size decimals
        double multiplier = std::pow(10.0, size_decimals);
        return std::round(quantized * multiplier) / multiplier;
    }
    
    /**
     * @brief Validate order parameters against trading rules
     * @return Error message if invalid, empty string if valid
     */
    std::string validate_order(double price, double size) const {
        // Check minimum size
        if (size < min_order_size) {
            return "Order size " + std::to_string(size) + 
                   " is below minimum " + std::to_string(min_order_size);
        }
        
        // Check maximum size
        if (max_order_size > 0.0 && size > max_order_size) {
            return "Order size " + std::to_string(size) + 
                   " exceeds maximum " + std::to_string(max_order_size);
        }
        
        // Check minimum price
        if (price < min_price) {
            return "Order price " + std::to_string(price) + 
                   " is below minimum " + std::to_string(min_price);
        }
        
        // Check maximum price
        if (max_price > 0.0 && price > max_price) {
            return "Order price " + std::to_string(price) + 
                   " exceeds maximum " + std::to_string(max_price);
        }
        
        // Check minimum notional
        double notional = price * size;
        if (min_notional > 0.0 && notional < min_notional) {
            return "Order notional " + std::to_string(notional) + 
                   " is below minimum " + std::to_string(min_notional);
        }
        
        return "";  // Valid
    }
};

} // namespace latentspeed::connector
