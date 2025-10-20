/**
 * @file perpetual_derivative_base.h
 * @brief Base class for perpetual derivative exchanges
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#pragma once

#include "connector/connector_base.h"
#include "connector/position.h"
#include <unordered_map>
#include <shared_mutex>

namespace latentspeed::connector {

/**
 * @class PerpetualDerivativeBase
 * @brief Base class for perpetual derivative exchanges
 * 
 * Adds derivative-specific functionality:
 * - Position management
 * - Leverage control
 * - Funding rate tracking
 * - Mark price / index price
 */
class PerpetualDerivativeBase : public ConnectorBase {
public:
    virtual ~PerpetualDerivativeBase() = default;

    // ========================================================================
    // CONNECTOR TYPE OVERRIDE
    // ========================================================================

    ConnectorType connector_type() const override {
        return ConnectorType::DERIVATIVE_PERPETUAL;
    }

    // ========================================================================
    // DERIVATIVE-SPECIFIC OPERATIONS
    // ========================================================================

    /**
     * @brief Set leverage for a symbol
     * @param symbol Trading symbol
     * @param leverage Leverage (e.g., 1, 5, 10, 20, 50, 100)
     * @return true if leverage was successfully set
     */
    virtual bool set_leverage(const std::string& symbol, int leverage) = 0;

    /**
     * @brief Get current position for a symbol
     * @param symbol Trading symbol
     * @return Position if exists, nullopt otherwise
     */
    virtual std::optional<Position> get_position(const std::string& symbol) const {
        std::shared_lock lock(positions_mutex_);
        auto it = positions_.find(symbol);
        if (it != positions_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Get all active positions
     * @return Vector of all positions
     */
    virtual std::vector<Position> get_all_positions() const {
        std::shared_lock lock(positions_mutex_);
        std::vector<Position> result;
        result.reserve(positions_.size());
        for (const auto& [_, position] : positions_) {
            result.push_back(position);
        }
        return result;
    }

    /**
     * @brief Get current funding rate for a symbol
     * @param symbol Trading symbol
     * @return Funding rate if available, nullopt otherwise
     */
    virtual std::optional<double> get_funding_rate(const std::string& symbol) const {
        std::shared_lock lock(funding_rates_mutex_);
        auto it = funding_rates_.find(symbol);
        if (it != funding_rates_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Get mark price for a symbol
     * @param symbol Trading symbol
     * @return Mark price if available, nullopt otherwise
     */
    virtual std::optional<double> get_mark_price(const std::string& symbol) const {
        std::shared_lock lock(prices_mutex_);
        auto it = mark_prices_.find(symbol);
        if (it != mark_prices_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Get index price for a symbol
     * @param symbol Trading symbol
     * @return Index price if available, nullopt otherwise
     */
    virtual std::optional<double> get_index_price(const std::string& symbol) const {
        std::shared_lock lock(prices_mutex_);
        auto it = index_prices_.find(symbol);
        if (it != index_prices_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Set position mode (one-way or hedge)
     * @param mode Position mode
     * @return true if successfully set
     */
    virtual bool set_position_mode(PositionMode mode) {
        position_mode_ = mode;
        return true;
    }

    /**
     * @brief Get current position mode
     */
    virtual PositionMode get_position_mode() const {
        return position_mode_;
    }

    /**
     * @brief Set listener for position events
     */
    virtual void set_position_event_listener(PositionEventListener* listener) {
        position_event_listener_ = listener;
    }

protected:
    // ========================================================================
    // POSITION MANAGEMENT (INTERNAL)
    // ========================================================================

    /**
     * @brief Update position from user stream
     * @param symbol Trading symbol
     * @param position Position data
     */
    virtual void update_position(const std::string& symbol, const Position& position) {
        std::unique_lock lock(positions_mutex_);
        
        // Update or insert
        positions_[symbol] = position;
        
        // Emit event
        if (position_event_listener_) {
            position_event_listener_->on_position_update(
                symbol,
                to_string(position.side),
                position.size,
                position.entry_price,
                position.unrealized_pnl
            );
        }
    }

    /**
     * @brief Remove position (when closed)
     */
    virtual void remove_position(const std::string& symbol) {
        std::unique_lock lock(positions_mutex_);
        positions_.erase(symbol);
    }

    /**
     * @brief Update funding rate from market data
     */
    virtual void update_funding_rate(const std::string& symbol, double rate) {
        std::unique_lock lock(funding_rates_mutex_);
        funding_rates_[symbol] = rate;
    }

    /**
     * @brief Update mark price
     */
    virtual void update_mark_price(const std::string& symbol, double price) {
        std::unique_lock lock(prices_mutex_);
        mark_prices_[symbol] = price;
    }

    /**
     * @brief Update index price
     */
    virtual void update_index_price(const std::string& symbol, double price) {
        std::unique_lock lock(prices_mutex_);
        index_prices_[symbol] = price;
    }

    // ========================================================================
    // MEMBER VARIABLES
    // ========================================================================

    // Position tracking
    std::unordered_map<std::string, Position> positions_;
    mutable std::shared_mutex positions_mutex_;

    // Funding rate cache
    std::unordered_map<std::string, double> funding_rates_;
    mutable std::shared_mutex funding_rates_mutex_;

    // Mark/Index price cache
    std::unordered_map<std::string, double> mark_prices_;
    std::unordered_map<std::string, double> index_prices_;
    mutable std::shared_mutex prices_mutex_;

    // Position mode
    PositionMode position_mode_ = PositionMode::ONE_WAY;

    // Event listener
    PositionEventListener* position_event_listener_ = nullptr;
};

} // namespace latentspeed::connector
