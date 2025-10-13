/**
 * @file exchange_interface.h
 * @brief Exchange abstraction layer for multi-exchange support
 * @author jessiondiwangan@gmail.com
 * @date 2025
 * 
 * Provides a unified interface for connecting to different crypto exchanges,
 * similar to cryptofeed's exchange abstraction but optimized for C++.
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include "market_data_provider.h"

namespace latentspeed {

/**
 * @struct ExchangeConfig
 * @brief Configuration for exchange connection
 */
struct ExchangeConfig {
    std::string name;                    ///< Exchange name (bybit, binance, etc.)
    std::vector<std::string> symbols;    ///< Symbols to subscribe
    bool enable_trades = true;           ///< Subscribe to trades
    bool enable_orderbook = true;        ///< Subscribe to orderbook
    bool snapshots_only = true;          ///< Orderbook snapshots only (vs deltas)
    int snapshot_interval = 1;           ///< Snapshot interval in seconds
    
    // Connection settings
    int reconnect_attempts = 10;         ///< Max reconnection attempts
    int reconnect_delay_ms = 5000;       ///< Delay between reconnections
    
    ExchangeConfig() = default;
    ExchangeConfig(const std::string& n, const std::vector<std::string>& s)
        : name(n), symbols(s) {}
};

/**
 * @class ExchangeInterface
 * @brief Abstract base class for exchange implementations
 * 
 * Each exchange (Bybit, Binance, etc.) implements this interface
 * to provide exchange-specific WebSocket URLs, subscription formats,
 * and message parsing logic.
 */
class ExchangeInterface {
public:
    virtual ~ExchangeInterface() = default;
    
    /**
     * @brief Get exchange name
     */
    virtual std::string get_name() const = 0;
    
    /**
     * @brief Get WebSocket connection details
     */
    virtual std::string get_websocket_host() const = 0;
    virtual std::string get_websocket_port() const = 0;
    virtual std::string get_websocket_target() const = 0;
    
    /**
     * @brief Generate subscription message for this exchange
     * @param symbols List of symbols to subscribe
     * @param enable_trades Subscribe to trades
     * @param enable_orderbook Subscribe to orderbook
     * @return JSON subscription message
     */
    virtual std::string generate_subscription(
        const std::vector<std::string>& symbols,
        bool enable_trades,
        bool enable_orderbook
    ) const = 0;
    
    /**
     * @brief Parse raw WebSocket message into trades/books
     * @param message Raw JSON message from exchange
     * @param tick Output trade tick (if applicable)
     * @param snapshot Output orderbook snapshot (if applicable)
     * @return MessageType (TRADE, BOOK, HEARTBEAT, ERROR)
     */
    enum class MessageType { TRADE, BOOK, HEARTBEAT, ERROR, UNKNOWN };
    
    virtual MessageType parse_message(
        const std::string& message,
        MarketTick& tick,
        OrderBookSnapshot& snapshot
    ) const = 0;
    
    /**
     * @brief Normalize symbol format for this exchange
     * @param symbol Raw symbol (e.g., "BTC-USDT" or "BTCUSDT")
     * @return Normalized symbol for this exchange
     */
    virtual std::string normalize_symbol(const std::string& symbol) const = 0;
};

/**
 * @class BybitExchange
 * @brief Bybit exchange implementation
 */
class BybitExchange : public ExchangeInterface {
public:
    std::string get_name() const override { return "BYBIT"; }
    
    std::string get_websocket_host() const override {
        return "stream.bybit.com";
    }
    
    std::string get_websocket_port() const override {
        return "443";
    }
    
    std::string get_websocket_target() const override {
        return "/v5/public/spot";
    }
    
    std::string generate_subscription(
        const std::vector<std::string>& symbols,
        bool enable_trades,
        bool enable_orderbook
    ) const override;
    
    MessageType parse_message(
        const std::string& message,
        MarketTick& tick,
        OrderBookSnapshot& snapshot
    ) const override;
    
    std::string normalize_symbol(const std::string& symbol) const override {
        // Bybit uses no separator: BTCUSDT
        std::string normalized = symbol;
        // Remove dash if present
        normalized.erase(std::remove(normalized.begin(), normalized.end(), '-'), 
                        normalized.end());
        return normalized;
    }
};

/**
 * @class BinanceExchange
 * @brief Binance exchange implementation
 */
class BinanceExchange : public ExchangeInterface {
public:
    std::string get_name() const override { return "BINANCE"; }
    
    std::string get_websocket_host() const override {
        return "stream.binance.com";
    }
    
    std::string get_websocket_port() const override {
        return "9443";
    }
    
    std::string get_websocket_target() const override {
        return "/ws";
    }
    
    std::string generate_subscription(
        const std::vector<std::string>& symbols,
        bool enable_trades,
        bool enable_orderbook
    ) const override;
    
    MessageType parse_message(
        const std::string& message,
        MarketTick& tick,
        OrderBookSnapshot& snapshot
    ) const override;
    
    std::string normalize_symbol(const std::string& symbol) const override {
        // Binance uses lowercase, no separator: btcusdt
        std::string normalized = symbol;
        normalized.erase(std::remove(normalized.begin(), normalized.end(), '-'), 
                        normalized.end());
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
        return normalized;
    }
};

/**
 * @class DydxExchange
 * @brief dYdX v4 exchange implementation
 */
class DydxExchange : public ExchangeInterface {
public:
    std::string get_name() const override { return "DYDX"; }
    
    std::string get_websocket_host() const override {
        return "indexer.dydx.trade";
    }
    
    std::string get_websocket_port() const override {
        return "443";
    }
    
    std::string get_websocket_target() const override {
        return "/v4/ws";
    }
    
    std::string generate_subscription(
        const std::vector<std::string>& symbols,
        bool enable_trades,
        bool enable_orderbook
    ) const override;
    
    MessageType parse_message(
        const std::string& message,
        MarketTick& tick,
        OrderBookSnapshot& snapshot
    ) const override;
    
    std::string normalize_symbol(const std::string& symbol) const override {
        // dYdX uses dash separator and uppercase: BTC-USD, ETH-USD
        std::string normalized = symbol;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);
        
        // Replace USDT with USD (dYdX uses USD not USDT)
        size_t pos = normalized.find("USDT");
        if (pos != std::string::npos) {
            normalized.replace(pos, 4, "USD");
        }
        
        // Ensure dash separator
        if (normalized.find('-') == std::string::npos && normalized.length() >= 6) {
            // Insert dash before last 3 chars (USD)
            normalized.insert(normalized.length() - 3, "-");
        }
        
        return normalized;
    }
};

/**
 * @class ExchangeFactory
 * @brief Factory for creating exchange instances
 */
class ExchangeFactory {
public:
    /**
     * @brief Create exchange instance by name
     * @param name Exchange name (case-insensitive)
     * @return Unique pointer to exchange instance
     */
    static std::unique_ptr<ExchangeInterface> create(const std::string& name) {
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        
        if (lower_name == "bybit") {
            return std::make_unique<BybitExchange>();
        } else if (lower_name == "binance") {
            return std::make_unique<BinanceExchange>();
        } else if (lower_name == "dydx") {
            return std::make_unique<DydxExchange>();
        } else {
            throw std::runtime_error("Unsupported exchange: " + name);
        }
    }
    
    /**
     * @brief Get list of supported exchanges
     */
    static std::vector<std::string> supported_exchanges() {
        return {"bybit", "binance", "dydx"};
    }
};

} // namespace latentspeed
