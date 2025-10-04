/**
 * @file exchange_client.h
 * @brief Abstract base class for exchange client implementations
 * @author jessiondiwangan@gmail.com
 * @date 2025
 * 
 * Direct exchange API interface, replacing CCAPI dependency
 */

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <map>
#include <optional>
#include <future>
#include <rapidjson/document.h>

namespace latentspeed {

/**
 * @struct OrderRequest
 * @brief Standard order request structure for all exchanges
 */
struct OrderRequest {
    std::string client_order_id;
    std::string symbol;
    std::string side;           // "buy" or "sell"
    std::string order_type;     // "limit" or "market"
    std::string quantity;
    std::optional<std::string> price;  // Required for limit orders
    std::optional<std::string> time_in_force;  // GTC, IOC, FOK, etc.
    std::optional<std::string> category;  // spot, linear, inverse, etc.
    bool reduce_only = false;   // CRITICAL: Position management for derivatives
    std::map<std::string, std::string> extra_params;
};

/**
 * @struct OrderResponse
 * @brief Standard order response structure
 */
struct OrderResponse {
    bool success;
    std::string message;
    std::optional<std::string> exchange_order_id;
    std::optional<std::string> client_order_id;
    std::optional<std::string> status;
    std::map<std::string, std::string> extra_data;
};

/**
 * @struct FillData
 * @brief Trade execution fill information
 */
struct FillData {
    std::string client_order_id;
    std::string exchange_order_id;
    std::string exec_id;
    std::string symbol;
    std::string side;
    std::string price;
    std::string quantity;
    std::string fee;
    std::string fee_currency;
    std::string liquidity;  // "maker" or "taker"
    uint64_t timestamp_ms;
    std::map<std::string, std::string> extra_data;
};

/**
 * @struct OrderUpdate
 * @brief Order status update information
 */
struct OrderUpdate {
    std::string client_order_id;
    std::string exchange_order_id;
    std::string status;  // "new", "partially_filled", "filled", "cancelled", "rejected"
    std::string reason;
    uint64_t timestamp_ms;
    std::optional<FillData> fill;  // Populated if this update includes a fill
};

/**
 * @struct OpenOrderBrief
 * @brief Brief information about an open order
 */
struct OpenOrderBrief {
    std::string client_order_id;
    std::string exchange_order_id; // exchange-assigned order id if available
    std::string symbol;
    std::string side;
    std::string order_type;
    std::string qty;
    bool        reduce_only{false};
    std::string category; // "spot" | "linear" | "inverse"
    std::map<std::string, std::string> extra;
};


/**
 * @class ExchangeClient
 * @brief Abstract base class for exchange client implementations
 * 
 * Provides a common interface for interacting with different cryptocurrency exchanges.
 * Each exchange should have its own implementation of this interface.
 */
class ExchangeClient {
public:
    using OrderUpdateCallback = std::function<void(const OrderUpdate&)>;
    using FillCallback = std::function<void(const FillData&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    ExchangeClient() = default;
    virtual ~ExchangeClient() = default;
    
    /**
     * @brief Initialize the exchange client
     * @param api_key API key for authentication
     * @param api_secret API secret for authentication
     * @param testnet Whether to use testnet/demo endpoints
     * @return true if initialization successful
     */
    virtual bool initialize(const std::string& api_key, 
                          const std::string& api_secret,
                          bool testnet = false) = 0;
    
    /**
     * @brief Connect to the exchange (both REST and WebSocket)
     * @return true if connection successful
     */
    virtual bool connect() = 0;
    
    /**
     * @brief Disconnect from the exchange
     */
    virtual void disconnect() = 0;
    
    /**
     * @brief Check if client is connected
     * @return true if connected
     */
    virtual bool is_connected() const = 0;
    
    /**
     * @brief Place a new order
     * @param request Order request details
     * @return Order response with result
     */
    virtual OrderResponse place_order(const OrderRequest& request) = 0;
    
    /**
     * @brief Cancel an existing order
     * @param client_order_id Client order ID to cancel
     * @param symbol Trading symbol (may be required by some exchanges)
     * @return Order response with result
     */
    virtual OrderResponse cancel_order(const std::string& client_order_id,
                                      const std::optional<std::string>& symbol = std::nullopt,
                                      const std::optional<std::string>& exchange_order_id = std::nullopt) = 0;
    
    /**
     * @brief Modify an existing order
     * @param client_order_id Client order ID to modify
     * @param new_quantity New order quantity (optional)
     * @param new_price New order price (optional)
     * @return Order response with result
     */
    virtual OrderResponse modify_order(const std::string& client_order_id,
                                      const std::optional<std::string>& new_quantity = std::nullopt,
                                      const std::optional<std::string>& new_price = std::nullopt) = 0;
    
    /**
     * @brief Query order status
     * @param client_order_id Client order ID to query
     * @return Order response with current status
     */
    virtual OrderResponse query_order(const std::string& client_order_id) = 0;
    
    /**
     * @brief Set callback for order updates
     * @param callback Function to call on order updates
     */
    virtual void set_order_update_callback(OrderUpdateCallback callback) = 0;
    
    /**
     * @brief Set callback for fills
     * @param callback Function to call on fills
     */
    virtual void set_fill_callback(FillCallback callback) = 0;
    
    /**
     * @brief Set callback for errors
     * @param callback Function to call on errors
     */
    virtual void set_error_callback(ErrorCallback callback) = 0;
    
    /**
     * @brief Get exchange name
     * @return Exchange name string
     */
    virtual std::string get_exchange_name() const = 0;
    
    /**
     * @brief Subscribe to order updates for specific symbols
     * @param symbols List of symbols to subscribe to
     * @return true if subscription successful
     */
    virtual bool subscribe_to_orders(const std::vector<std::string>& symbols = {}) = 0;

    /**
     * @brief List all open orders with optional filters
     * @param category Optional category filter (e.g. "spot", "linear", "inverse")
     * @param symbol Optional symbol filter (e.g. "BTCUSDT")
     * @param settle_coin Optional settlement coin filter (e.g. "USDT", "USD")
     * @param base_coin Optional base coin filter (e.g. "BTC", "ETH")
     * @return Vector of open orders matching the filters
     */
    virtual std::vector<OpenOrderBrief> list_open_orders(
    const std::optional<std::string>& category    = std::nullopt,
    const std::optional<std::string>& symbol      = std::nullopt,
    const std::optional<std::string>& settle_coin = std::nullopt,
    const std::optional<std::string>& base_coin   = std::nullopt) 
{
    return {};
}
    
protected:
    OrderUpdateCallback order_update_callback_;
    FillCallback fill_callback_;
    ErrorCallback error_callback_;
    
    bool is_testnet_ = false;
    std::string api_key_;
    std::string api_secret_;
};

} // namespace latentspeed
