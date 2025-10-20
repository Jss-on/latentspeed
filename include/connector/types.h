/**
 * @file types.h
 * @brief Common types and enums for connector framework
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#pragma once

#include <string>
#include <string_view>

namespace latentspeed::connector {

/**
 * @enum ConnectorType
 * @brief Type of exchange connector
 */
enum class ConnectorType {
    SPOT,                   // Spot trading
    DERIVATIVE_PERPETUAL,   // Perpetual futures/swaps
    DERIVATIVE_FUTURES,     // Dated futures
    AMM_DEX,                // Automated Market Maker DEX (Uniswap, etc.)
    ORDERBOOK_DEX           // Orderbook-based DEX
};

/**
 * @enum OrderType
 * @brief Type of order
 */
enum class OrderType {
    LIMIT,          // Standard limit order
    MARKET,         // Market order (immediate execution)
    LIMIT_MAKER,    // Post-only limit order (must be maker)
    STOP_LIMIT,     // Stop-limit order
    STOP_MARKET     // Stop-market order
};

/**
 * @enum TradeType
 * @brief Side of the trade
 */
enum class TradeType {
    BUY,    // Buy/Long
    SELL    // Sell/Short
};

/**
 * @enum PositionAction
 * @brief Action on position (for derivatives)
 */
enum class PositionAction {
    NIL,    // Not specified (spot or first entry)
    OPEN,   // Open new position
    CLOSE   // Close existing position (reduce-only)
};

/**
 * @enum PositionSide
 * @brief Position side (for derivatives)
 */
enum class PositionSide {
    LONG,   // Long position
    SHORT,  // Short position
    BOTH    // Both (hedge mode)
};

/**
 * @enum PositionMode
 * @brief Position mode (for derivatives)
 */
enum class PositionMode {
    ONE_WAY,    // One-way mode (net position)
    HEDGE       // Hedge mode (separate long/short)
};

// ============================================================================
// STRING CONVERSION FUNCTIONS
// ============================================================================

/**
 * @brief Convert ConnectorType to string
 */
inline std::string to_string(ConnectorType type) {
    switch (type) {
        case ConnectorType::SPOT: return "SPOT";
        case ConnectorType::DERIVATIVE_PERPETUAL: return "DERIVATIVE_PERPETUAL";
        case ConnectorType::DERIVATIVE_FUTURES: return "DERIVATIVE_FUTURES";
        case ConnectorType::AMM_DEX: return "AMM_DEX";
        case ConnectorType::ORDERBOOK_DEX: return "ORDERBOOK_DEX";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Convert OrderType to string
 */
inline std::string to_string(OrderType type) {
    switch (type) {
        case OrderType::LIMIT: return "LIMIT";
        case OrderType::MARKET: return "MARKET";
        case OrderType::LIMIT_MAKER: return "LIMIT_MAKER";
        case OrderType::STOP_LIMIT: return "STOP_LIMIT";
        case OrderType::STOP_MARKET: return "STOP_MARKET";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Convert TradeType to string
 */
inline std::string to_string(TradeType type) {
    return type == TradeType::BUY ? "BUY" : "SELL";
}

/**
 * @brief Convert PositionAction to string
 */
inline std::string to_string(PositionAction action) {
    switch (action) {
        case PositionAction::NIL: return "NIL";
        case PositionAction::OPEN: return "OPEN";
        case PositionAction::CLOSE: return "CLOSE";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Convert PositionSide to string
 */
inline std::string to_string(PositionSide side) {
    switch (side) {
        case PositionSide::LONG: return "LONG";
        case PositionSide::SHORT: return "SHORT";
        case PositionSide::BOTH: return "BOTH";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Convert PositionMode to string
 */
inline std::string to_string(PositionMode mode) {
    return mode == PositionMode::ONE_WAY ? "ONE_WAY" : "HEDGE";
}

/**
 * @brief Helper to check if order type is limit-based
 */
inline bool is_limit_type(OrderType type) {
    return type == OrderType::LIMIT || 
           type == OrderType::LIMIT_MAKER || 
           type == OrderType::STOP_LIMIT;
}

/**
 * @brief Helper to check if order type is market-based
 */
inline bool is_market_type(OrderType type) {
    return type == OrderType::MARKET || 
           type == OrderType::STOP_MARKET;
}

} // namespace latentspeed::connector
