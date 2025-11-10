/**
 * @file hyperliquid_connector_adapter.cpp
 * @brief Implementation of HyperliquidConnectorAdapter bridge
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#include "adapters/hyperliquid/hyperliquid_connector_adapter.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>

namespace latentspeed {

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

HyperliquidConnectorAdapter::HyperliquidConnectorAdapter()
    : testnet_(false) {
    spdlog::info("[HyperliquidAdapter] Bridge adapter created");
}

HyperliquidConnectorAdapter::~HyperliquidConnectorAdapter() {
    disconnect();
    spdlog::info("[HyperliquidAdapter] Bridge adapter destroyed");
}

// ============================================================================
// LIFECYCLE
// ============================================================================

bool HyperliquidConnectorAdapter::initialize(
    const std::string& api_key,
    const std::string& api_secret,
    bool testnet) {
    
    if (initialized_.load()) {
        spdlog::warn("[HyperliquidAdapter] Already initialized");
        return true;
    }

    try {
        testnet_ = testnet;
        
        spdlog::info("[HyperliquidAdapter] Initializing Hummingbot-pattern connector...");
        spdlog::info("[HyperliquidAdapter] Testnet: {}", testnet);
        
        // Create auth module
        auth_ = std::make_shared<connector::HyperliquidAuth>(api_key, api_secret);
        
        // Create connector with auth
        connector_ = std::make_shared<connector::HyperliquidPerpetualConnector>(auth_, testnet);
        
        // Set up event forwarding from connector to adapter
        setup_event_forwarding();
        
        // Initialize the connector
        if (!connector_->initialize()) {
            spdlog::error("[HyperliquidAdapter] Failed to initialize connector");
            return false;
        }
        
        initialized_ = true;
        spdlog::info("[HyperliquidAdapter] Initialization complete");
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("[HyperliquidAdapter] Initialization failed: {}", e.what());
        return false;
    }
}

bool HyperliquidConnectorAdapter::connect() {
    if (!initialized_.load()) {
        spdlog::error("[HyperliquidAdapter] Cannot connect: not initialized");
        return false;
    }
    
    if (connected_.load()) {
        spdlog::warn("[HyperliquidAdapter] Already connected");
        return true;
    }

    try {
        spdlog::info("[HyperliquidAdapter] Connecting to Hyperliquid...");
        
        // Start the connector (WebSocket connections, etc.)
        connector_->start();
        
        // Retry logic: wait up to 3 seconds with exponential backoff
        constexpr int MAX_RETRIES = 6;
        constexpr int BASE_DELAY_MS = 250;
        
        for (int retry = 0; retry < MAX_RETRIES; ++retry) {
            int delay_ms = BASE_DELAY_MS * (1 << retry);  // 250, 500, 1000, 2000...
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            
            if (connector_->is_connected()) {
                connected_ = true;
                spdlog::info("[HyperliquidAdapter] Connected successfully after {} ms", 
                           BASE_DELAY_MS * ((1 << (retry + 1)) - 1));
                return true;
            }
            
            if (retry < MAX_RETRIES - 1) {
                spdlog::debug("[HyperliquidAdapter] Connection not ready, retry {}/{}", 
                            retry + 1, MAX_RETRIES);
            }
        }
        
        // Connection started but not fully ready after retries
        spdlog::warn("[HyperliquidAdapter] Connection started but not fully ready after {} retries. "
                     "Will continue; user stream may come online momentarily.", MAX_RETRIES);
        connected_ = false;
        return true;  // Allow engine to proceed
        
    } catch (const std::exception& e) {
        spdlog::error("[HyperliquidAdapter] Connection failed: {}", e.what());
        return false;
    }
}

void HyperliquidConnectorAdapter::disconnect() {
    if (!connected_.load()) {
        return;
    }
    
    spdlog::info("[HyperliquidAdapter] Disconnecting...");
    
    if (connector_) {
        connector_->stop();
    }
    
    connected_ = false;
    spdlog::info("[HyperliquidAdapter] Disconnected");
}

bool HyperliquidConnectorAdapter::is_connected() const {
    return connected_.load() && connector_ && connector_->is_connected();
}

// ============================================================================
// ORDER OPERATIONS
// ============================================================================

OrderResponse HyperliquidConnectorAdapter::place_order(const OrderRequest& request) {
    if (!is_connected()) {
        return OrderResponse{
            .success = false,
            .message = "Not connected"
        };
    }

    try {
        // Translate OrderRequest → OrderParams
        auto params = translate_to_order_params(request);
        
        // Determine trade type (buy/sell)
        bool is_buy = (request.side == "buy" || request.side == "Buy" || request.side == "BUY");
        
        // Place order via connector (non-blocking, returns client_order_id immediately)
        std::string client_order_id;
        if (is_buy) {
            client_order_id = connector_->buy(params);
        } else {
            client_order_id = connector_->sell(params);
        }
        
        spdlog::info("[HyperliquidAdapter] Order placed: {} ({})", 
                     client_order_id, request.side);
        
        // Return immediate response (exchange_order_id comes later via callback)
        return OrderResponse{
            .success = true,
            .message = "Order placed",
            .client_order_id = client_order_id
        };
        
    } catch (const std::exception& e) {
        spdlog::error("[HyperliquidAdapter] Order placement failed: {}", e.what());
        return OrderResponse{
            .success = false,
            .message = e.what()
        };
    }
}

OrderResponse HyperliquidConnectorAdapter::cancel_order(
    const std::string& client_order_id,
    const std::optional<std::string>& symbol,
    const std::optional<std::string>& exchange_order_id) {
    
    if (!is_connected()) {
        return OrderResponse{
            .success = false,
            .message = "Not connected",
            .client_order_id = client_order_id
        };
    }

    try {
        // Cancel via connector (synchronous for simplicity)
        bool success = connector_->cancel(client_order_id);
        
        if (success) {
            spdlog::info("[HyperliquidAdapter] Order cancelled: {}", client_order_id);
            return OrderResponse{
                .success = true,
                .message = "Order cancelled",
                .client_order_id = client_order_id
            };
        } else {
            return OrderResponse{
                .success = false,
                .message = "Cancel request failed",
                .client_order_id = client_order_id
            };
        }
        
    } catch (const std::exception& e) {
        spdlog::error("[HyperliquidAdapter] Cancel failed: {}", e.what());
        return OrderResponse{
            .success = false,
            .message = e.what(),
            .client_order_id = client_order_id
        };
    }
}

OrderResponse HyperliquidConnectorAdapter::modify_order(
    const std::string& client_order_id,
    const std::optional<std::string>& new_quantity,
    const std::optional<std::string>& new_price) {
    
    if (!is_connected()) {
        return OrderResponse{
            .success = false,
            .message = "Not connected",
            .client_order_id = client_order_id
        };
    }

    try {
        spdlog::info("[HyperliquidAdapter] Modify via cancel + replace: {}", client_order_id);
        
        // Get original order details
        auto order_opt = connector_->get_order(client_order_id);
        if (!order_opt.has_value()) {
            return OrderResponse{
                .success = false,
                .message = "Original order not found for modify",
                .client_order_id = client_order_id
            };
        }
        
        const auto& original_order = *order_opt;
        
        // Cancel original order
        bool cancel_success = connector_->cancel(client_order_id);
        if (!cancel_success) {
            return OrderResponse{
                .success = false,
                .message = "Failed to cancel original order for modify",
                .client_order_id = client_order_id
            };
        }
        
        spdlog::info("[HyperliquidAdapter] Cancelled original order {}, placing replacement", 
                     client_order_id);
        
        // Brief delay to allow cancel to propagate
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Build replacement order with modified parameters
        connector::OrderParams params;
        params.trading_pair = original_order.trading_pair;
        params.order_type = original_order.order_type;
        params.position_action = original_order.position_action;
        
        // Use new values if provided, otherwise keep original
        if (new_quantity.has_value()) {
            params.amount = std::stod(*new_quantity);
        } else {
            params.amount = original_order.amount;
        }
        
        if (new_price.has_value()) {
            params.price = std::stod(*new_price);
        } else {
            params.price = original_order.price;
        }
        
        // Place replacement order
        std::string new_client_order_id;
        if (original_order.trade_type == connector::TradeType::BUY) {
            new_client_order_id = connector_->buy(params);
        } else {
            new_client_order_id = connector_->sell(params);
        }
        
        spdlog::info("[HyperliquidAdapter] Replacement order placed: {} (original: {})",
                     new_client_order_id, client_order_id);
        
        return OrderResponse{
            .success = true,
            .message = "Order modified via cancel + replace",
            .client_order_id = new_client_order_id,
            .extra_data = {{"original_client_order_id", client_order_id}}
        };
        
    } catch (const std::exception& e) {
        spdlog::error("[HyperliquidAdapter] Modify failed: {}", e.what());
        return OrderResponse{
            .success = false,
            .message = std::string("Modify failed: ") + e.what(),
            .client_order_id = client_order_id
        };
    }
}

OrderResponse HyperliquidConnectorAdapter::query_order(const std::string& client_order_id) {
    if (!is_connected()) {
        return OrderResponse{
            .success = false,
            .message = "Not connected",
            .client_order_id = client_order_id
        };
    }

    try {
        // Query order from connector's tracker
        auto order_opt = connector_->get_order(client_order_id);
        
        if (!order_opt.has_value()) {
            return OrderResponse{
                .success = false,
                .message = "Order not found",
                .client_order_id = client_order_id
            };
        }
        
        const auto& order = *order_opt;
        
        return OrderResponse{
            .success = true,
            .message = "Order found",
            .exchange_order_id = order.exchange_order_id,
            .client_order_id = client_order_id
        };
        
    } catch (const std::exception& e) {
        spdlog::error("[HyperliquidAdapter] Query failed: {}", e.what());
        return OrderResponse{
            .success = false,
            .message = e.what(),
            .client_order_id = client_order_id
        };
    }
}

// ============================================================================
// CALLBACKS
// ============================================================================

void HyperliquidConnectorAdapter::set_order_update_callback(OrderUpdateCallback cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    order_update_cb_ = std::move(cb);
    spdlog::debug("[HyperliquidAdapter] Order update callback registered");
}

void HyperliquidConnectorAdapter::set_fill_callback(FillCallback cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    fill_cb_ = std::move(cb);
    spdlog::debug("[HyperliquidAdapter] Fill callback registered");
}

void HyperliquidConnectorAdapter::set_error_callback(ErrorCallback cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    error_cb_ = std::move(cb);
    spdlog::debug("[HyperliquidAdapter] Error callback registered");
}

// ============================================================================
// OPEN ORDER REHYDRATION
// ============================================================================

std::vector<OpenOrderBrief> HyperliquidConnectorAdapter::list_open_orders(
    const std::optional<std::string>& category,
    const std::optional<std::string>& symbol,
    const std::optional<std::string>& settle_coin,
    const std::optional<std::string>& base_coin) {
    
    std::vector<OpenOrderBrief> briefs;
    
    if (!is_connected()) {
        spdlog::warn("[HyperliquidAdapter] Cannot list orders: not connected");
        return briefs;
    }

    try {
        // Get open orders from connector
        auto orders = connector_->get_open_orders();
        
        // Translate to OpenOrderBrief format
        for (const auto& order : orders) {
            briefs.push_back(translate_to_open_order_brief(order));
        }
        
        spdlog::info("[HyperliquidAdapter] Listed {} open orders", briefs.size());
        
    } catch (const std::exception& e) {
        spdlog::error("[HyperliquidAdapter] Failed to list orders: {}", e.what());
    }
    
    return briefs;
}

// ============================================================================
// TRANSLATION METHODS
// ============================================================================

connector::OrderParams HyperliquidConnectorAdapter::translate_to_order_params(
    const OrderRequest& request) {
    
    connector::OrderParams params;
    
    // Symbol normalization
    params.trading_pair = normalize_symbol(request.symbol);
    
    // Amount and price
    params.amount = std::stod(request.quantity);
    params.price = request.price.has_value() ? std::stod(*request.price) : 0.0;
    
    // Order type translation
    std::string order_type_lower = request.order_type;
    std::transform(order_type_lower.begin(), order_type_lower.end(),
                   order_type_lower.begin(), ::tolower);
    
    if (order_type_lower == "limit") {
        params.order_type = connector::OrderType::LIMIT;
    } else if (order_type_lower == "market") {
        params.order_type = connector::OrderType::MARKET;
    } else if (order_type_lower == "limit_maker" || order_type_lower == "post_only") {
        params.order_type = connector::OrderType::LIMIT_MAKER;
    } else {
        params.order_type = connector::OrderType::LIMIT; // Default
    }
    
    // Position action (reduce_only) - check both direct field and extra_params
    if (request.reduce_only || 
        (request.extra_params.count("reduce_only") && 
         (request.extra_params.at("reduce_only") == "true" || 
          request.extra_params.at("reduce_only") == "True" ||
          request.extra_params.at("reduce_only") == "1"))) {
        params.position_action = connector::PositionAction::CLOSE;
    } else {
        params.position_action = connector::PositionAction::OPEN;
    }
    
    // Copy extra parameters
    params.extra_params = request.extra_params;
    
    return params;
}

OrderResponse HyperliquidConnectorAdapter::translate_to_order_response(
    const std::string& client_order_id,
    bool success,
    const std::string& error_msg) {
    
    return OrderResponse{
        .success = success,
        .message = error_msg,
        .client_order_id = client_order_id
    };
}

OpenOrderBrief HyperliquidConnectorAdapter::translate_to_open_order_brief(
    const connector::InFlightOrder& order) {
    
    OpenOrderBrief brief;
    brief.client_order_id = order.client_order_id;
    brief.symbol = order.trading_pair;
    
    // Trade type to side
    brief.side = (order.trade_type == connector::TradeType::BUY) ? "Buy" : "Sell";
    
    // Order type
    switch (order.order_type) {
        case connector::OrderType::LIMIT:
            brief.order_type = "Limit";
            break;
        case connector::OrderType::MARKET:
            brief.order_type = "Market";
            break;
        case connector::OrderType::LIMIT_MAKER:
            brief.order_type = "PostOnly";
            break;
        default:
            brief.order_type = "Limit";
    }
    
    // Quantities
    brief.qty = std::to_string(order.amount);
    brief.reduce_only = (order.position_action == connector::PositionAction::CLOSE);
    
    // Category (Hyperliquid is perpetual only)
    brief.category = "linear";
    
    // Exchange order ID if available
    if (order.exchange_order_id.has_value()) {
        brief.extra["orderId"] = *order.exchange_order_id;
    }
    
    return brief;
}

std::string HyperliquidConnectorAdapter::normalize_symbol(const std::string& symbol) {
    // Convert various formats to Hyperliquid format: "BASE-USD"
    std::string normalized = symbol;
    
    // Remove common suffixes
    const std::vector<std::string> suffixes = {"USDT", "USD", "PERP", "-PERP"};
    for (const auto& suffix : suffixes) {
        if (normalized.size() > suffix.size()) {
            size_t pos = normalized.rfind(suffix);
            if (pos == normalized.size() - suffix.size()) {
                normalized = normalized.substr(0, pos);
            }
        }
    }
    
    // If no hyphen, it's compact format like "BTC" or "ETH"
    if (normalized.find('-') == std::string::npos && 
        normalized.find('/') == std::string::npos) {
        // Already in base format, add -USD
        return normalized + "-USD";
    }
    
    // Replace / with -
    std::replace(normalized.begin(), normalized.end(), '/', '-');
    
    // Ensure -USD suffix
    if (normalized.find("-USD") == std::string::npos) {
        normalized += "-USD";
    }
    
    return normalized;
}

std::string HyperliquidConnectorAdapter::extract_base(const std::string& symbol) {
    std::string normalized = normalize_symbol(symbol);
    size_t pos = normalized.find('-');
    if (pos != std::string::npos) {
        return normalized.substr(0, pos);
    }
    return normalized;
}

// ============================================================================
// EVENT FORWARDING
// ============================================================================

void HyperliquidConnectorAdapter::setup_event_forwarding() {
    if (!connector_) {
        return;
    }
    
    // Create event listener for connector
    class AdapterEventListener : public connector::OrderEventListener {
    public:
        AdapterEventListener(HyperliquidConnectorAdapter* adapter) : adapter_(adapter) {}
        
        void on_order_created(const std::string& client_order_id,
                            const std::string& exchange_order_id) override {
            adapter_->forward_order_event("created", client_order_id, exchange_order_id);
        }
        
        void on_order_filled(const std::string& client_order_id,
                            double fill_price,
                            double fill_amount) override {
            adapter_->forward_trade_event(client_order_id, fill_price, fill_amount);
        }
        
        void on_order_completed(const std::string& client_order_id,
                               double avg_price,
                               double total_filled) override {
            adapter_->forward_order_event("completed", client_order_id);
        }
        
        void on_order_cancelled(const std::string& client_order_id) override {
            adapter_->forward_order_event("cancelled", client_order_id);
        }
        
        void on_order_failed(const std::string& client_order_id,
                            const std::string& reason) override {
            adapter_->forward_order_event("failed", client_order_id);
        }
        
    private:
        HyperliquidConnectorAdapter* adapter_;
    };
    
    // Set the listener on connector
    auto listener = new AdapterEventListener(this);
    connector_->set_order_event_listener(listener);
    
    spdlog::info("[HyperliquidAdapter] Event forwarding configured");
}

void HyperliquidConnectorAdapter::forward_order_event(
    const std::string& event_type,
    const std::string& client_order_id,
    const std::string& exchange_order_id) {
    
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    
    if (!order_update_cb_) {
        return;
    }
    
    // Create OrderUpdate for engine
    OrderUpdate update;
    update.client_order_id = client_order_id;
    update.exchange_order_id = exchange_order_id;
    update.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // Map event type to status
    if (event_type == "created") {
        update.status = "new";
        update.reason = "Order created";
    } else if (event_type == "completed" || event_type == "filled") {
        update.status = "filled";
        update.reason = "Order filled";
    } else if (event_type == "cancelled") {
        update.status = "cancelled";
        update.reason = "Order cancelled";
    } else if (event_type == "failed") {
        update.status = "rejected";
        update.reason = "Order rejected";
    } else {
        update.status = "partially_filled";
        update.reason = "Order partially filled";
    }
    
    // Forward to engine callback
    order_update_cb_(update);
}

void HyperliquidConnectorAdapter::forward_trade_event(
    const std::string& client_order_id,
    double fill_price,
    double fill_amount) {
    
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    
    if (!fill_cb_) {
        return;
    }
    
    // Get order details to extract fee information from trade fills
    auto order_opt = connector_->get_order(client_order_id);
    if (!order_opt.has_value()) {
        spdlog::warn("[HyperliquidAdapter] Cannot forward trade for unknown order: {}", 
                     client_order_id);
        return;
    }
    
    const auto& order = *order_opt;
    
    // Extract fee and liquidity from most recent trade fill (if available)
    double fee_amount = 0.0;
    std::string fee_currency = "USDC";
    std::string exec_id = "";
    std::string liquidity = "taker";  // Default
    
    if (!order.trade_fills.empty()) {
        // Get the most recent fill that matches this price/amount
        for (auto it = order.trade_fills.rbegin(); it != order.trade_fills.rend(); ++it) {
            if (std::abs(it->fill_price - fill_price) < 1e-8 && 
                std::abs(it->fill_base_amount - fill_amount) < 1e-8) {
                fee_amount = it->fee_amount;
                fee_currency = it->fee_currency;
                exec_id = it->trade_id;
                // Extract liquidity flag (maker/taker)
                if (it->liquidity.has_value()) {
                    liquidity = *it->liquidity;
                }
                break;
            }
        }
        
        // Fallback: use latest fill if no exact match
        if (exec_id.empty() && !order.trade_fills.empty()) {
            const auto& latest = order.trade_fills.back();
            fee_amount = latest.fee_amount;
            fee_currency = latest.fee_currency;
            exec_id = latest.trade_id;
            if (latest.liquidity.has_value()) {
                liquidity = *latest.liquidity;
            }
        }
    }
    
    // Create FillData for engine with actual fee and liquidity information
    FillData fill;
    fill.client_order_id = client_order_id;
    fill.exec_id = exec_id.empty() ? std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) : exec_id;
    fill.price = std::to_string(fill_price);
    fill.quantity = std::to_string(fill_amount);
    fill.fee = std::to_string(fee_amount);
    fill.fee_currency = fee_currency;
    fill.symbol = order.trading_pair;
    fill.side = (order.trade_type == connector::TradeType::BUY) ? "Buy" : "Sell";
    fill.liquidity = liquidity;  // Actual maker/taker flag from connector
    
    if (order.exchange_order_id.has_value()) {
        fill.exchange_order_id = *order.exchange_order_id;
    }
    
    spdlog::debug("[HyperliquidAdapter] Forwarding fill: {} @ {} (fee: {} {})",
                 fill.quantity, fill.price, fill.fee, fill.fee_currency);
    
    // Forward to engine callback
    fill_cb_(fill);
}

} // namespace latentspeed
