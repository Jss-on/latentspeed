/**
 * @file binance_client.h
 * @brief Binance exchange client scaffold (USDT-M futures first)
 *
 * Minimal implementation to integrate with the TradingEngineService wiring.
 * Full REST/WS logic can be implemented incrementally behind this interface.
 */

#pragma once

#include "exchange/exchange_client.h"
#include <atomic>
#include <string>
#include <vector>
#include <optional>
#include <thread>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <deque>
#include <mutex>

namespace latentspeed {

class BinanceClient final : public ExchangeClient {
public:
    BinanceClient() = default;
    ~BinanceClient() override = default;

    // Lifecycle
    bool initialize(const std::string& api_key,
                    const std::string& api_secret,
                    bool testnet = false) override;
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;

    // Orders
    OrderResponse place_order(const OrderRequest& request) override;
    OrderResponse cancel_order(const std::string& client_order_id,
                               const std::optional<std::string>& symbol = std::nullopt,
                               const std::optional<std::string>& exchange_order_id = std::nullopt) override;
    OrderResponse modify_order(const std::string& client_order_id,
                               const std::optional<std::string>& new_quantity = std::nullopt,
                               const std::optional<std::string>& new_price = std::nullopt) override;
    OrderResponse query_order(const std::string& client_order_id) override;

    // Subscriptions
    bool subscribe_to_orders(const std::vector<std::string>& symbols = {}) override;

    // Metadata
    std::string get_exchange_name() const override { return "binance"; }

    // Callback setters
    void set_order_update_callback(OrderUpdateCallback callback) override { order_update_callback_ = std::move(callback); }
    void set_fill_callback(FillCallback callback) override { fill_callback_ = std::move(callback); }
    void set_error_callback(ErrorCallback callback) override { error_callback_ = std::move(callback); }

    // Open orders listing
    std::vector<OpenOrderBrief> list_open_orders(
        const std::optional<std::string>& category    = std::nullopt,
        const std::optional<std::string>& symbol      = std::nullopt,
        const std::optional<std::string>& settle_coin = std::nullopt,
        const std::optional<std::string>& base_coin   = std::nullopt) override;

private:
    // Endpoint configuration
    void configure_endpoints(bool testnet);

    // REST helpers
    std::string hmac_sha256(const std::string& key, const std::string& data) const;
    uint64_t timestamp_ms() const;
    // Sends a REST request. If signed==true, appends timestamp/recvWindow/signature.
    // For POST, the query is sent as x-www-form-urlencoded body; for GET/DELETE it is appended to URL.
    bool rest_request(const std::string& method,
                      const std::string& path,
                      const std::string& query,
                      bool signed_req,
                      long& http_status,
                      std::string& response_body);

    // Listen key lifecycle (user data WS)
    bool create_listen_key(std::string& out_listen_key);
    bool keepalive_listen_key(const std::string& listen_key);
    void listenkey_keepalive_loop();

    // Map service request to Binance types
    std::string map_side(const std::string& side_lower) const;      // buy/sell -> BUY/SELL
    std::string map_type(const std::string& type_lower, bool post_only) const; // limit/market -> LIMIT/MARKET/GTX
    std::string map_time_in_force(const std::optional<std::string>& tif_opt, bool post_only) const; // GTC/IOC/FOK/GTX

    // Endpoints
    std::string rest_base_;      // e.g. https://fapi.binance.com
    std::string ws_user_base_;   // e.g. wss://fstream.binance.com

    // WS trading optional (not implemented yet)
    bool use_ws_trading_{false};

    // Connection state
    std::atomic<bool> connected_{false};
    std::atomic<bool> stop_{false};
    std::string active_listen_key_;
    std::unique_ptr<std::thread> listenkey_thread_;
    std::unique_ptr<std::thread> ws_thread_;

    // User data WS processor
    void ws_user_thread();
    void process_user_ws_message(const std::string& msg);

    // Symbol filters cache for rounding
    struct SymbolFilters {
        double tick_size{0.0};
        double step_size{0.0};
        double min_qty{0.0};
        double min_notional{0.0};
        int price_decimals{0};
        int qty_decimals{0};
    };
    std::unordered_map<std::string, SymbolFilters> filters_;
    std::mutex filters_mtx_;
    bool get_symbol_filters(const std::string& symbol, SymbolFilters& out);
    static int decimals_from_step(const std::string& step);
    static std::string format_decimal(double v, int decimals);
    static std::string trim_zeros(std::string s);

    // Pending orders snapshot for quick symbol lookup and cancel/query assist
    std::map<std::string, OrderRequest> pending_orders_;
    std::mutex orders_mutex_;

    // Fill deduplication by exec id
    std::unordered_set<std::string> seen_exec_ids_;
    std::mutex seen_exec_mutex_;

    // REST rate limiter (basic)
    struct RateLimiter {
        std::mutex mtx;
        std::deque<std::chrono::steady_clock::time_point> history;
        size_t max_per_window{8};
        std::chrono::milliseconds window{std::chrono::milliseconds(1000)};
        void throttle() {
            using clock = std::chrono::steady_clock;
            std::unique_lock<std::mutex> lk(mtx);
            auto now = clock::now();
            while (!history.empty() && (now - history.front()) > window) history.pop_front();
            if (history.size() >= max_per_window) {
                auto wait_for = window - std::chrono::duration_cast<std::chrono::milliseconds>(now - history.front());
                lk.unlock();
                std::this_thread::sleep_for(wait_for);
                lk.lock();
                now = clock::now();
                while (!history.empty() && (now - history.front()) > window) history.pop_front();
            }
            history.push_back(now);
        }
    } rest_rate_limiter_;

    // WS health/backoff
    std::atomic<bool> ws_healthy_{false};
    uint32_t ws_backoff_attempt_{0};
};

} // namespace latentspeed
