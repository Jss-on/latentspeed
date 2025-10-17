/**
 * @file exchange_adapter.h
 * @brief Adapter interface to abstract venue-specific clients behind a common surface.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <functional>

#include "exchange/exchange_client.h"  // reuse OrderRequest/Response/Update/FillData/OpenOrderBrief

namespace latentspeed {

/**
 * @class IExchangeAdapter
 * @brief Phase 1 adapter interface that mirrors ExchangeClient to minimize code churn.
 *
 * This allows TradingEngineService to depend on adapters, while existing concrete
 * clients (BybitClient, BinanceClient, ...) are wrapped without behavior changes.
 */
class IExchangeAdapter {
public:
    using Ptr = std::unique_ptr<IExchangeAdapter>;
    using OrderUpdateCallback = ExchangeClient::OrderUpdateCallback;
    using FillCallback = ExchangeClient::FillCallback;
    using ErrorCallback = ExchangeClient::ErrorCallback;

    virtual ~IExchangeAdapter() = default;

    // Lifecycle
    virtual bool initialize(const std::string& api_key,
                            const std::string& api_secret,
                            bool testnet) = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;

    // Order ops
    virtual OrderResponse place_order(const OrderRequest& request) = 0;
    virtual OrderResponse cancel_order(const std::string& client_order_id,
                                       const std::optional<std::string>& symbol = std::nullopt,
                                       const std::optional<std::string>& exchange_order_id = std::nullopt) = 0;
    virtual OrderResponse modify_order(const std::string& client_order_id,
                                       const std::optional<std::string>& new_quantity = std::nullopt,
                                       const std::optional<std::string>& new_price = std::nullopt) = 0;
    virtual OrderResponse query_order(const std::string& client_order_id) = 0;

    // Subscriptions / callbacks
    virtual void set_order_update_callback(OrderUpdateCallback cb) = 0;
    virtual void set_fill_callback(FillCallback cb) = 0;
    virtual void set_error_callback(ErrorCallback cb) = 0;

    // Discovery
    virtual std::string get_exchange_name() const = 0;

    // Open-order rehydration (optional)
    virtual std::vector<OpenOrderBrief> list_open_orders(
        const std::optional<std::string>& category = std::nullopt,
        const std::optional<std::string>& symbol = std::nullopt,
        const std::optional<std::string>& settle_coin = std::nullopt,
        const std::optional<std::string>& base_coin = std::nullopt) = 0;
};

} // namespace latentspeed

