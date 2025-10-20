/**
 * @file connector_base.h
 * @brief Abstract base class for all exchange connectors (Hummingbot pattern)
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#pragma once

#include <string>
#include <memory>
#include <optional>
#include <functional>
#include <vector>
#include <atomic>
#include <map>

#include "connector/types.h"
#include "connector/events.h"
#include "connector/trading_rule.h"

namespace latentspeed::connector {

// Forward declarations (full definitions in Phase 2)
class InFlightOrder;
class ClientOrderTracker;

/**
 * @struct OrderParams
 * @brief Parameters for placing an order
 */
struct OrderParams {
    std::string trading_pair;
    double amount;
    double price;
    OrderType order_type;
    PositionAction position_action = PositionAction::NIL;
    std::optional<int> leverage;
    std::optional<double> trigger_price;
    std::map<std::string, std::string> extra_params;
};

/**
 * @class ConnectorBase
 * @brief Abstract base class for all exchange connectors (Hummingbot pattern)
 * 
 * This class defines the contract that all exchange connectors must implement.
 * It follows Hummingbot's ConnectorBase design with async order placement,
 * order tracking, and event-driven updates.
 * 
 * Key Patterns from Hummingbot:
 * 1. Non-blocking order placement (returns client_order_id immediately)
 * 2. Start tracking BEFORE API call
 * 3. Event-driven state updates via WebSocket
 * 4. Separate market data and user data sources
 */
class ConnectorBase {
public:
    virtual ~ConnectorBase() = default;

    // ========================================================================
    // IDENTITY & LIFECYCLE
    // ========================================================================

    /**
     * @brief Get the connector name (e.g., "hyperliquid_perpetual")
     */
    virtual std::string name() const = 0;

    /**
     * @brief Get the domain (e.g., "hyperliquid_perpetual" or "hyperliquid_perpetual_testnet")
     */
    virtual std::string domain() const = 0;

    /**
     * @brief Get the connector type
     */
    virtual ConnectorType connector_type() const = 0;

    /**
     * @brief Initialize the connector with credentials
     * @return true if initialization successful
     */
    virtual bool initialize() = 0;

    /**
     * @brief Connect to the exchange (WebSocket, gRPC, etc.)
     * @return true if connection successful
     */
    virtual bool connect() = 0;

    /**
     * @brief Disconnect from the exchange
     */
    virtual void disconnect() = 0;

    /**
     * @brief Check if connector is connected
     */
    virtual bool is_connected() const = 0;

    /**
     * @brief Check if trading is ready (connected + authenticated + market data available)
     */
    virtual bool is_ready() const = 0;

    // ========================================================================
    // ORDER PLACEMENT (PUBLIC API - HUMMINGBOT PATTERN)
    // ========================================================================

    /**
     * @brief Place a BUY order (async, non-blocking)
     * @param params Order parameters
     * @return Client order ID (generated immediately)
     * 
     * IMPORTANT: This returns immediately with a client order ID.
     * The actual order placement happens asynchronously.
     * Use event listeners to track order status.
     */
    virtual std::string buy(const OrderParams& params) = 0;

    /**
     * @brief Place a SELL order (async, non-blocking)
     * @param params Order parameters
     * @return Client order ID (generated immediately)
     */
    virtual std::string sell(const OrderParams& params) = 0;

    /**
     * @brief Cancel an order
     * @param client_order_id Client order ID from buy()/sell()
     * @return true if cancel request submitted successfully
     */
    virtual bool cancel(const std::string& client_order_id) = 0;

    // ========================================================================
    // EVENT LISTENERS
    // ========================================================================

    /**
     * @brief Set listener for order events (created, filled, cancelled, etc.)
     */
    virtual void set_order_event_listener(OrderEventListener* listener) {
        order_event_listener_ = listener;
    }

    /**
     * @brief Set listener for trade/fill events
     */
    virtual void set_trade_event_listener(TradeEventListener* listener) {
        trade_event_listener_ = listener;
    }

    /**
     * @brief Set listener for error events
     */
    virtual void set_error_event_listener(ErrorEventListener* listener) {
        error_event_listener_ = listener;
    }

    /**
     * @brief Set listener for balance events
     */
    virtual void set_balance_event_listener(BalanceEventListener* listener) {
        balance_event_listener_ = listener;
    }

    // ========================================================================
    // METADATA & RULES
    // ========================================================================

    /**
     * @brief Get trading rules for a specific trading pair
     */
    virtual std::optional<TradingRule> get_trading_rule(const std::string& trading_pair) const = 0;

    /**
     * @brief Get all trading rules
     */
    virtual std::vector<TradingRule> get_all_trading_rules() const = 0;

    /**
     * @brief Get current timestamp in nanoseconds
     */
    virtual uint64_t current_timestamp_ns() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    // ========================================================================
    // UTILITY METHODS (IMPLEMENTED IN BASE)
    // ========================================================================

    /**
     * @brief Generate a new unique client order ID
     */
    virtual std::string generate_client_order_id();

    /**
     * @brief Quantize order price according to trading rules
     */
    virtual double quantize_order_price(const std::string& trading_pair, double price) const;

    /**
     * @brief Quantize order amount according to trading rules
     */
    virtual double quantize_order_amount(const std::string& trading_pair, double amount) const;

    /**
     * @brief Get client order ID prefix (e.g., "LS" for LatentSpeed)
     */
    virtual std::string get_client_order_id_prefix() const {
        return client_order_id_prefix_;
    }

    /**
     * @brief Set client order ID prefix
     */
    virtual void set_client_order_id_prefix(const std::string& prefix) {
        client_order_id_prefix_ = prefix;
    }

protected:
    // ========================================================================
    // EVENT EMISSION HELPERS
    // ========================================================================

    void emit_order_created_event(const std::string& client_order_id, 
                                   const std::string& exchange_order_id);
    
    void emit_order_filled_event(const std::string& client_order_id,
                                  double fill_price, double fill_amount);
    
    void emit_order_completed_event(const std::string& client_order_id,
                                     double avg_price, double total_filled);
    
    void emit_order_cancelled_event(const std::string& client_order_id);
    
    void emit_order_failed_event(const std::string& client_order_id,
                                  const std::string& reason);
    
    void emit_error_event(const std::string& error_code,
                          const std::string& error_message);

    // ========================================================================
    // MEMBER VARIABLES
    // ========================================================================

    // Event listeners (raw pointers for observer pattern)
    OrderEventListener* order_event_listener_ = nullptr;
    TradeEventListener* trade_event_listener_ = nullptr;
    ErrorEventListener* error_event_listener_ = nullptr;
    BalanceEventListener* balance_event_listener_ = nullptr;

    // Client order ID generation
    std::string client_order_id_prefix_ = "LS";  // LatentSpeed
    std::atomic<uint64_t> order_id_counter_{0};
};

} // namespace latentspeed::connector
