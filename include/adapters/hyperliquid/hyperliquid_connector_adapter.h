/**
 * @file hyperliquid_connector_adapter.h
 * @brief Bridge adapter that wraps HyperliquidPerpetualConnector (Hummingbot pattern)
 *        to implement IExchangeAdapter interface for trading engine integration
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#pragma once

#include "adapters/exchange_adapter.h"
#include "connector/exchange/hyperliquid/hyperliquid_perpetual_connector.h"
#include "connector/exchange/hyperliquid/hyperliquid_auth.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <future>
#include <atomic>

namespace latentspeed {

/**
 * @class HyperliquidConnectorAdapter
 * @brief Bridge adapter that allows Hummingbot-pattern connector to work with
 *        existing trading engine infrastructure
 * 
 * This adapter translates between:
 * - IExchangeAdapter (simple wrapper interface) ← Trading engine expects this
 * - HyperliquidPerpetualConnector (Hummingbot pattern) ← Full-featured connector
 * 
 * Design Pattern: Adapter/Bridge
 * Thread Safety: Yes (internal locking)
 * Lifecycle: initialize() → connect() → place/cancel/query → disconnect()
 */
class HyperliquidConnectorAdapter final : public IExchangeAdapter {
public:
    HyperliquidConnectorAdapter();
    ~HyperliquidConnectorAdapter() override;

    // ========================================================================
    // LIFECYCLE (IExchangeAdapter)
    // ========================================================================

    bool initialize(const std::string& api_key,
                    const std::string& api_secret,
                    bool testnet) override;
    
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;

    // ========================================================================
    // ORDER OPERATIONS (IExchangeAdapter)
    // ========================================================================

    OrderResponse place_order(const OrderRequest& request) override;
    
    OrderResponse cancel_order(const std::string& client_order_id,
                               const std::optional<std::string>& symbol = std::nullopt,
                               const std::optional<std::string>& exchange_order_id = std::nullopt) override;
    
    OrderResponse modify_order(const std::string& client_order_id,
                               const std::optional<std::string>& new_quantity = std::nullopt,
                               const std::optional<std::string>& new_price = std::nullopt) override;
    
    OrderResponse query_order(const std::string& client_order_id) override;

    // ========================================================================
    // CALLBACKS (IExchangeAdapter)
    // ========================================================================

    void set_order_update_callback(OrderUpdateCallback cb) override;
    void set_fill_callback(FillCallback cb) override;
    void set_error_callback(ErrorCallback cb) override;

    // ========================================================================
    // DISCOVERY (IExchangeAdapter)
    // ========================================================================

    std::string get_exchange_name() const override { return "hyperliquid"; }

    // ========================================================================
    // OPEN ORDER REHYDRATION (IExchangeAdapter)
    // ========================================================================

    std::vector<OpenOrderBrief> list_open_orders(
        const std::optional<std::string>& category = std::nullopt,
        const std::optional<std::string>& symbol = std::nullopt,
        const std::optional<std::string>& settle_coin = std::nullopt,
        const std::optional<std::string>& base_coin = std::nullopt) override;

private:
    // ========================================================================
    // TRANSLATION METHODS
    // ========================================================================

    /**
     * @brief Translate OrderRequest (engine) → OrderParams (connector)
     */
    connector::OrderParams translate_to_order_params(const OrderRequest& request);

    /**
     * @brief Translate connector result → OrderResponse (engine)
     */
    OrderResponse translate_to_order_response(
        const std::string& client_order_id,
        bool success,
        const std::string& error_msg = "");

    /**
     * @brief Translate InFlightOrder (connector) → OpenOrderBrief (engine)
     */
    OpenOrderBrief translate_to_open_order_brief(const connector::InFlightOrder& order);

    /**
     * @brief Normalize symbol format
     * @param symbol Input symbol (various formats)
     * @return Normalized symbol for Hyperliquid (e.g., "BTC-USD")
     */
    std::string normalize_symbol(const std::string& symbol);

    /**
     * @brief Extract base currency from symbol
     * @param symbol Trading pair (e.g., "BTC-USD", "BTCUSDT")
     * @return Base currency (e.g., "BTC")
     */
    std::string extract_base(const std::string& symbol);

    // ========================================================================
    // EVENT FORWARDING (Connector → Adapter → Engine)
    // ========================================================================

    /**
     * @brief Set up event listeners on the connector
     */
    void setup_event_forwarding();

    /**
     * @brief Forward connector order events to engine callbacks
     */
    void forward_order_event(
        const std::string& event_type,
        const std::string& client_order_id,
        const std::string& exchange_order_id = "");

    /**
     * @brief Forward connector trade events to engine callbacks
     */
    void forward_trade_event(
        const std::string& client_order_id,
        double fill_price,
        double fill_amount);

    // ========================================================================
    // INTERNAL STATE MANAGEMENT
    // ========================================================================

    /**
     * @brief Track pending async operations
     */
    struct PendingOperation {
        std::string client_order_id;
        std::chrono::steady_clock::time_point start_time;
        std::promise<OrderResponse> promise;
    };

    void register_pending_operation(const std::string& client_order_id,
                                    std::promise<OrderResponse>&& promise);
    
    void complete_pending_operation(const std::string& client_order_id,
                                    const OrderResponse& response);

    // ========================================================================
    // MEMBER VARIABLES
    // ========================================================================

    // Core connector (Hummingbot pattern)
    std::shared_ptr<connector::HyperliquidPerpetualConnector> connector_;
    
    // Authentication
    std::shared_ptr<connector::HyperliquidAuth> auth_;
    bool testnet_;
    
    // Callbacks from engine
    OrderUpdateCallback order_update_cb_;
    FillCallback fill_cb_;
    ErrorCallback error_cb_;
    
    // Thread safety
    mutable std::mutex callbacks_mutex_;
    mutable std::mutex operations_mutex_;
    
    // Pending operations tracking
    std::unordered_map<std::string, PendingOperation> pending_operations_;
    
    // Connection state
    std::atomic<bool> initialized_{false};
    std::atomic<bool> connected_{false};
};

} // namespace latentspeed
