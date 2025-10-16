/**
 * @file hyperliquid_adapter.h
 * @brief Skeleton adapter for Hyperliquid implementing IExchangeAdapter (Phase 3 - M1 scaffold).
 */

#pragma once

#include <memory>
#include <string>
#include <optional>
#include <vector>

#include "adapters/exchange_adapter.h"
#include "adapters/hyperliquid_config.h"
namespace latentspeed::netws { class HlWsPostClient; }

// Forward declare resolver to avoid coupling in header consumers
namespace latentspeed { class HyperliquidAssetResolver; }

namespace latentspeed {

class HyperliquidAdapter final : public IExchangeAdapter {
public:
    HyperliquidAdapter();
    ~HyperliquidAdapter() override;

    // Lifecycle
    bool initialize(const std::string& api_key,
                    const std::string& api_secret,
                    bool testnet) override;
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;

    // Order ops
    OrderResponse place_order(const OrderRequest& request) override;
    OrderResponse cancel_order(const std::string& client_order_id,
                               const std::optional<std::string>& symbol = std::nullopt,
                               const std::optional<std::string>& exchange_order_id = std::nullopt) override;
    OrderResponse modify_order(const std::string& client_order_id,
                               const std::optional<std::string>& new_quantity = std::nullopt,
                               const std::optional<std::string>& new_price = std::nullopt) override;
    OrderResponse query_order(const std::string& client_order_id) override;

    // Subscriptions / callbacks
    void set_order_update_callback(OrderUpdateCallback cb) override;
    void set_fill_callback(FillCallback cb) override;
    void set_error_callback(ErrorCallback cb) override;

    // Discovery
    std::string get_exchange_name() const override { return "hyperliquid"; }

    // Open-order rehydration
    std::vector<OpenOrderBrief> list_open_orders(
        const std::optional<std::string>& category = std::nullopt,
        const std::optional<std::string>& symbol = std::nullopt,
        const std::optional<std::string>& settle_coin = std::nullopt,
        const std::optional<std::string>& base_coin = std::nullopt) override;

private:
    // M1 scaffold state only; real client wiring arrives in later milestones
    bool connected_{false};
    bool testnet_{false};
    std::string api_key_;
    std::string api_secret_;
    HyperliquidConfig cfg_ { HyperliquidConfig::for_network(false) };
    std::unique_ptr<HyperliquidAssetResolver> resolver_;
    // Nonce + Signer (to be used in later milestones)
    std::unique_ptr<class HyperliquidNonceManager> nonce_mgr_;
    std::unique_ptr<class IHyperliquidSigner> signer_;
    std::unique_ptr<latentspeed::netws::HlWsPostClient> ws_post_;

    // Callbacks (stored for when WS is implemented in later milestones)
    OrderUpdateCallback order_update_cb_{};
    FillCallback fill_cb_{};
    ErrorCallback error_cb_{};
};

} // namespace latentspeed
