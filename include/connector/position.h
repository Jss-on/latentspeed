/**
 * @file position.h
 * @brief Position representation for derivatives trading
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#pragma once

#include <string>
#include <optional>
#include "connector/types.h"

namespace latentspeed::connector {

/**
 * @struct Position
 * @brief Represents an open position in derivatives trading
 */
struct Position {
    std::string symbol;              // Trading symbol
    PositionSide side;               // LONG or SHORT
    double size;                     // Position size (positive for both LONG and SHORT)
    double entry_price;              // Average entry price
    double mark_price;               // Current mark price
    double liquidation_price;        // Liquidation price
    double unrealized_pnl;           // Unrealized profit/loss
    double realized_pnl;             // Realized profit/loss
    int leverage;                    // Current leverage
    double margin;                   // Position margin
    uint64_t timestamp;              // Last update timestamp
    
    // Optional fields
    std::optional<double> funding_fee;  // Accumulated funding fee
    std::optional<std::string> position_id;  // Exchange position ID
    
    /**
     * @brief Check if position is long
     */
    bool is_long() const {
        return side == PositionSide::LONG;
    }
    
    /**
     * @brief Check if position is short
     */
    bool is_short() const {
        return side == PositionSide::SHORT;
    }
    
    /**
     * @brief Calculate position value
     */
    double position_value() const {
        return size * mark_price;
    }
    
    /**
     * @brief Calculate return on equity (ROE)
     */
    double roe() const {
        if (margin <= 0.0) return 0.0;
        return (unrealized_pnl / margin) * 100.0;
    }
    
    /**
     * @brief Calculate distance to liquidation (percentage)
     */
    double distance_to_liquidation() const {
        if (mark_price <= 0.0) return 0.0;
        return std::abs((liquidation_price - mark_price) / mark_price) * 100.0;
    }
};

} // namespace latentspeed::connector
