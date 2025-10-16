/**
 * @file binance_adapter.h
 * @brief Adapter wrapping BinanceClient to the IExchangeAdapter interface (Phase 1).
 */

#pragma once

#include <memory>
#include <string>
#include "adapters/exchange_adapter.h"
#include "exchange/binance_client.h"

namespace latentspeed {

class BinanceAdapter final : public IExchangeAdapter {
public:
    BinanceAdapter();
    ~BinanceAdapter() override = default;

    bool initialize(const std::string& api_key,
                    const std::string& api_secret,
                    bool testnet) override;
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;

    OrderResponse place_order(const OrderRequest& request) override;
    OrderResponse cancel_order(const std::string& client_order_id,
                               const std::optional<std::string>& symbol = std::nullopt,
                               const std::optional<std::string>& exchange_order_id = std::nullopt) override;
    OrderResponse modify_order(const std::string& client_order_id,
                               const std::optional<std::string>& new_quantity = std::nullopt,
                               const std::optional<std::string>& new_price = std::nullopt) override;
    OrderResponse query_order(const std::string& client_order_id) override;

    void set_order_update_callback(OrderUpdateCallback cb) override;
    void set_fill_callback(FillCallback cb) override;
    void set_error_callback(ErrorCallback cb) override;

    std::string get_exchange_name() const override { return "binance"; }

    std::vector<OpenOrderBrief> list_open_orders(
        const std::optional<std::string>& category = std::nullopt,
        const std::optional<std::string>& symbol = std::nullopt,
        const std::optional<std::string>& settle_coin = std::nullopt,
        const std::optional<std::string>& base_coin = std::nullopt) override;

private:
    std::unique_ptr<BinanceClient> client_;
};

} // namespace latentspeed

