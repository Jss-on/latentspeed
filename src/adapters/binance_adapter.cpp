/**
 * @file binance_adapter.cpp
 * @brief Adapter wrapping BinanceClient to the IExchangeAdapter interface (Phase 1).
 */

#include "adapters/binance_adapter.h"

namespace latentspeed {

BinanceAdapter::BinanceAdapter() : client_(std::make_unique<BinanceClient>()) {}

bool BinanceAdapter::initialize(const std::string& api_key,
                                const std::string& api_secret,
                                bool testnet) {
    return client_->initialize(api_key, api_secret, testnet);
}

bool BinanceAdapter::connect() { return client_->connect(); }
void  BinanceAdapter::disconnect() { client_->disconnect(); }
bool  BinanceAdapter::is_connected() const { return client_->is_connected(); }

OrderResponse BinanceAdapter::place_order(const OrderRequest& request) {
    return client_->place_order(request);
}

OrderResponse BinanceAdapter::cancel_order(const std::string& client_order_id,
                                           const std::optional<std::string>& symbol,
                                           const std::optional<std::string>& exchange_order_id) {
    return client_->cancel_order(client_order_id, symbol, exchange_order_id);
}

OrderResponse BinanceAdapter::modify_order(const std::string& client_order_id,
                                           const std::optional<std::string>& new_quantity,
                                           const std::optional<std::string>& new_price) {
    return client_->modify_order(client_order_id, new_quantity, new_price);
}

OrderResponse BinanceAdapter::query_order(const std::string& client_order_id) {
    return client_->query_order(client_order_id);
}

void BinanceAdapter::set_order_update_callback(OrderUpdateCallback cb) { client_->set_order_update_callback(std::move(cb)); }
void BinanceAdapter::set_fill_callback(FillCallback cb) { client_->set_fill_callback(std::move(cb)); }
void BinanceAdapter::set_error_callback(ErrorCallback cb) { client_->set_error_callback(std::move(cb)); }

std::vector<OpenOrderBrief> BinanceAdapter::list_open_orders(
    const std::optional<std::string>& category,
    const std::optional<std::string>& symbol,
    const std::optional<std::string>& settle_coin,
    const std::optional<std::string>& base_coin) {
    (void)settle_coin; (void)base_coin; // Binance client signature may differ; ignore extras.
    // The current BinanceClient likely queries by symbol; if category unsupported, adapter can ignore.
    return client_->list_open_orders(category, symbol, std::nullopt, std::nullopt);
}

} // namespace latentspeed

