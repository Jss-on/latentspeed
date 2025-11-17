/**
 * @file hyperliquid_perpetual_connector.cpp
 * @brief Implementation of HyperliquidPerpetualConnector
 */

#include "connector/exchange/hyperliquid/hyperliquid_perpetual_connector.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>
#include <functional>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace latentspeed::connector {

// Helper: Convert client_order_id to 128-bit hex string for Hyperliquid cloid
// Format: 0x + 32 hex characters
static std::string convert_to_hex_cloid(const std::string& client_order_id) {
    // Use std::hash to generate a deterministic hash
    std::hash<std::string> hasher;
    size_t hash1 = hasher(client_order_id);
    size_t hash2 = hasher(client_order_id + "_salt");
    
    // Convert to 128-bit hex (we'll use two 64-bit hashes)
    std::ostringstream oss;
    oss << "0x"
        << std::hex << std::setfill('0') 
        << std::setw(16) << hash1
        << std::setw(16) << hash2;
    
    return oss.str();
}

// Constructor
HyperliquidPerpetualConnector::HyperliquidPerpetualConnector(
    std::shared_ptr<HyperliquidAuth> auth,
    bool testnet
)   : auth_(auth),
      testnet_(testnet),
      order_tracker_(),
      io_context_(),
      work_guard_(net::make_work_guard(io_context_)),
      running_(false) {
    
    // Set URLs based on testnet flag
    if (testnet_) {
        rest_host_ = "api.hyperliquid-testnet.xyz";
        rest_base_url_ = "https://api.hyperliquid-testnet.xyz";
        ws_host_ = "api.hyperliquid-testnet.xyz";
    } else {
        rest_host_ = "api.hyperliquid.xyz";
        rest_base_url_ = "https://api.hyperliquid.xyz";
        ws_host_ = "api.hyperliquid.xyz";
    }
    
    // Create data sources with appropriate URLs
    orderbook_data_source_ = std::make_shared<HyperliquidOrderBookDataSource>(testnet_);
    user_stream_data_source_ = std::make_shared<HyperliquidUserStreamDataSource>(auth, testnet_);
    
    // Set up user stream callback
    user_stream_data_source_->set_message_callback(
        [this](const UserStreamMessage& msg) {
            handle_user_stream_message(msg);
        }
    );
    
    // Start async worker thread
    async_thread_ = std::thread([this]() { io_context_.run(); });
}

// Destructor
HyperliquidPerpetualConnector::~HyperliquidPerpetualConnector() {
    stop();
}

// Lifecycle Methods
bool HyperliquidPerpetualConnector::initialize() {
    try {
        // Initialize data sources
        if (!orderbook_data_source_->initialize()) {
            spdlog::error("Failed to initialize orderbook data source");
            return false;
        }
        
        if (!user_stream_data_source_->initialize()) {
            spdlog::error("Failed to initialize user stream data source");
            return false;
        }
        
        // Fetch trading rules and asset metadata
        fetch_trading_rules();
        
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize HyperliquidPerpetualConnector: {}", e.what());
        return false;
    }
}

void HyperliquidPerpetualConnector::start() {
    if (running_) return;
    
    running_ = true;
    
    // Start data sources
    orderbook_data_source_->start();
    user_stream_data_source_->start();
    
    spdlog::info("HyperliquidPerpetualConnector started");
}

void HyperliquidPerpetualConnector::stop() {
    if (!running_) return;
    
    running_ = false;
    
    // Stop data sources
    orderbook_data_source_->stop();
    user_stream_data_source_->stop();
    
    // Stop async worker
    work_guard_.reset();
    io_context_.stop();
    
    if (async_thread_.joinable()) {
        async_thread_.join();
    }
    
    spdlog::info("HyperliquidPerpetualConnector stopped");
}

bool HyperliquidPerpetualConnector::is_connected() const {
    return user_stream_data_source_->is_connected();
}

// Order Placement
std::string HyperliquidPerpetualConnector::buy(const OrderParams& params) {
    return place_order(params, TradeType::BUY);
}

std::string HyperliquidPerpetualConnector::sell(const OrderParams& params) {
    return place_order(params, TradeType::SELL);
}

std::future<bool> HyperliquidPerpetualConnector::cancel(
    const std::string& trading_pair,
    const std::string& client_order_id) {
    
    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();
    
    // Schedule async cancellation
    net::post(io_context_, [this, trading_pair, client_order_id, promise]() {
        try {
            bool result = execute_cancel(trading_pair, client_order_id);
            promise->set_value(result);
        } catch (const std::exception& e) {
            spdlog::error("Cancel failed: {}", e.what());
            promise->set_exception(std::current_exception());
        }
    });
    
    return future;
}

// Order Tracking Access
std::optional<InFlightOrder> HyperliquidPerpetualConnector::get_order(
    const std::string& client_order_id) const {
    return order_tracker_.get_order(client_order_id);
}

std::vector<InFlightOrder> HyperliquidPerpetualConnector::get_open_orders() const {
    auto fillable_orders = order_tracker_.all_fillable_orders();
    std::vector<InFlightOrder> result;
    result.reserve(fillable_orders.size());
    for (const auto& [_, order] : fillable_orders) {
        result.push_back(order);
    }
    return result;
}

// Event Listener
void HyperliquidPerpetualConnector::set_event_listener(
    std::shared_ptr<OrderEventListener> listener) {
    event_listener_ = listener;
}

// ConnectorBase Implementations
std::string HyperliquidPerpetualConnector::name() const {
    return "hyperliquid_perpetual";
}

std::string HyperliquidPerpetualConnector::domain() const {
    return testnet_ ? "hyperliquid_perpetual_testnet" : "hyperliquid_perpetual";
}

ConnectorType HyperliquidPerpetualConnector::connector_type() const {
    return ConnectorType::DERIVATIVE_PERPETUAL;
}

bool HyperliquidPerpetualConnector::connect() {
    start();
    return true;
}

void HyperliquidPerpetualConnector::disconnect() {
    stop();
}

bool HyperliquidPerpetualConnector::is_ready() const {
    return running_ && orderbook_data_source_->is_connected() && user_stream_data_source_->is_connected();
}

bool HyperliquidPerpetualConnector::cancel(const std::string& client_order_id) {
    try {
        auto order_opt = order_tracker_.get_order(client_order_id);
        if (!order_opt) {
            return false;
        }
        
        const auto& order = *order_opt;
        if (!order.exchange_order_id) {
            return false;
        }
        
        return execute_cancel_order(order);
    } catch (const std::exception& e) {
        spdlog::error("Failed to cancel order {}: {}", client_order_id, e.what());
        return false;
    }
}

std::string HyperliquidPerpetualConnector::get_connector_name() const {
    return domain();
}

std::optional<TradingRule> HyperliquidPerpetualConnector::get_trading_rule(
    const std::string& trading_pair) const {
    std::lock_guard<std::mutex> lock(trading_rules_mutex_);
    auto it = trading_rules_.find(trading_pair);
    if (it != trading_rules_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<TradingRule> HyperliquidPerpetualConnector::get_all_trading_rules() const {
    std::lock_guard<std::mutex> lock(trading_rules_mutex_);
    std::vector<TradingRule> rules;
    for (const auto& [_, rule] : trading_rules_) {
        rules.push_back(rule);
    }
    return rules;
}

uint64_t HyperliquidPerpetualConnector::current_timestamp_ns() const {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// Private Methods
std::string HyperliquidPerpetualConnector::place_order(
    const OrderParams& params,
    TradeType trade_type) {
    
    // 1. Generate client order ID
    std::string client_order_id = generate_client_order_id();
    
    // 2. Validate params
    if (!validate_order_params(params)) {
        emit_order_failure_event(client_order_id, "Invalid order parameters");
        return client_order_id;
    }
    
    // 3. Apply trading rules (quantization)
    double quantized_price = quantize_order_price(params.trading_pair, params.price);
    double quantized_amount = quantize_order_amount(params.trading_pair, params.amount);
    
    // 4. Create InFlightOrder
    InFlightOrder order;
    order.client_order_id = client_order_id;
    order.trading_pair = params.trading_pair;
    order.order_type = params.order_type;
    order.trade_type = trade_type;
    order.position_action = params.position_action;
    order.price = quantized_price;
    order.amount = quantized_amount;
    order.creation_timestamp = current_timestamp_ns();
    
    // Add cloid (use client_order_id as default)
    if (params.extra_params.count("cloid")) {
        order.cloid = params.extra_params.at("cloid");
    } else {
        order.cloid = client_order_id;
    }
    
    // 5. START TRACKING BEFORE API CALL (Hummingbot critical pattern!)
    order_tracker_.start_tracking(std::move(order));
    
    // 6. Schedule async submission
    net::post(io_context_, [this, client_order_id]() {
        place_order_and_process_update(client_order_id);
    });
    
    // 7. Return immediately (NON-BLOCKING!)
    return client_order_id;
}

void HyperliquidPerpetualConnector::place_order_and_process_update(
    const std::string& client_order_id) {
    
    auto order_opt = order_tracker_.get_order(client_order_id);
    if (!order_opt.has_value()) {
        spdlog::error("Order {} not found in tracker", client_order_id);
        return;
    }
    
    InFlightOrder order = *order_opt;
    
    try {
        // Update state to PENDING_SUBMIT
        OrderUpdate pending_update;
        pending_update.client_order_id = client_order_id;
        pending_update.new_state = OrderState::PENDING_SUBMIT;
        pending_update.update_timestamp = current_timestamp_ns();
        order_tracker_.process_order_update(pending_update);
        
        // Call exchange API
        auto [exchange_order_id, timestamp] = execute_place_order(order);
        
        // Process success
        OrderUpdate success_update;
        success_update.client_order_id = client_order_id;
        success_update.exchange_order_id = exchange_order_id;
        success_update.trading_pair = order.trading_pair;
        success_update.new_state = OrderState::OPEN;
        success_update.update_timestamp = timestamp;
        order_tracker_.process_order_update(success_update);
        
        // Emit event
        emit_order_created_event(client_order_id, exchange_order_id);
        
        spdlog::info("Order {} created successfully with exchange ID {}", 
                    client_order_id, exchange_order_id);
        
    } catch (const std::exception& e) {
        // Process failure
        OrderUpdate failure_update;
        failure_update.client_order_id = client_order_id;
        failure_update.new_state = OrderState::FAILED;
        failure_update.update_timestamp = current_timestamp_ns();
        failure_update.reason = e.what();
        order_tracker_.process_order_update(failure_update);
        
        // Emit event
        emit_order_failure_event(client_order_id, e.what());
        
        spdlog::error("Order {} failed: {}", client_order_id, e.what());
    }
}

std::pair<std::string, uint64_t> HyperliquidPerpetualConnector::execute_place_order(
    const InFlightOrder& order) {
    
    // 1. Get asset index
    std::string coin = extract_coin_from_pair(order.trading_pair);
    
    if (coin_to_asset_.find(coin) == coin_to_asset_.end()) {
        throw std::runtime_error("Unknown asset: " + coin);
    }
    int asset_index = coin_to_asset_[coin];
    
    // 2. Map order type to Hyperliquid format
    nlohmann::json param_order_type = {{"limit", {{"tif", "Gtc"}}}};
    if (order.order_type == OrderType::LIMIT_MAKER) {
        param_order_type = {{"limit", {{"tif", "Alo"}}}};  // Post-only
    } else if (order.order_type == OrderType::MARKET) {
        param_order_type = {{"limit", {{"tif", "Ioc"}}}};  // Immediate or cancel
    }
    
    // 3. Convert price and size to wire format using trading rules
    auto rule_opt = get_trading_rule(order.trading_pair);
    int size_decimals = HyperliquidWebUtils::get_default_size_decimals(coin);
    int price_decimals = 5;  // Hyperliquid default
    
    if (rule_opt.has_value()) {
        size_decimals = rule_opt->size_decimals;
        price_decimals = rule_opt->price_decimals;
    }
    
    // For market orders (IOC), use extreme price to guarantee immediate execution
    double effective_price = order.price;
    if (order.order_type == OrderType::MARKET) {
        if (order.trade_type == TradeType::BUY) {
            // Buy: use very high price to match any ask
            effective_price = 1000000.0;
        } else {
            // Sell: use very low price to match any bid
            effective_price = 0.01;
        }
        spdlog::debug("[HL] Market order: using price {} for {} order", 
                     effective_price, order.trade_type == TradeType::BUY ? "BUY" : "SELL");
    }
    
    std::string limit_px = HyperliquidWebUtils::float_to_wire(effective_price, price_decimals);
    std::string sz = HyperliquidWebUtils::float_to_wire(order.amount, size_decimals);
    
    // 4. Convert client_order_id to valid cloid format (128-bit hex string)
    // Hyperliquid requires: 0x + 32 hex chars (128 bits)
    // Use hash of client_order_id to generate deterministic hex string
    std::string cloid = convert_to_hex_cloid(order.client_order_id);
    spdlog::info("[HL] Converted cloid: {} -> {}", order.client_order_id, cloid);
    
    // 5. Build request
    nlohmann::json action = {
        {"type", "order"},
        {"grouping", "na"},
        {"orders", nlohmann::json::array({
            {
                {"a", asset_index},
                {"b", order.trade_type == TradeType::BUY},
                {"p", limit_px},
                {"s", sz},
                {"r", order.position_action == PositionAction::CLOSE},
                {"t", param_order_type},
                {"c", cloid}
            }
        })}
    };
    
    // 6. Sign and send
    auto order_result = api_post_with_auth("/exchange", action);
    
    // 7. Parse response
    if (order_result["status"] == "err") {
        throw std::runtime_error(order_result["response"].dump());
    }
    
    const auto& status = order_result["response"]["data"]["statuses"][0];
    if (status.contains("error")) {
        throw std::runtime_error(status["error"].template get<std::string>());
    }
    
    // Extract exchange order ID
    std::string exchange_order_id;
    if (status.contains("resting")) {
        exchange_order_id = std::to_string(status["resting"]["oid"].template get<int64_t>());
    } else if (status.contains("filled")) {
        exchange_order_id = std::to_string(status["filled"]["oid"].template get<int64_t>());
    } else {
        throw std::runtime_error("Unexpected order status");
    }
    
    return {exchange_order_id, current_timestamp_ns()};
}

bool HyperliquidPerpetualConnector::execute_cancel(
    const std::string& trading_pair,
    const std::string& client_order_id) {
    
    auto order_opt = order_tracker_.get_order(client_order_id);
    if (!order_opt.has_value()) {
        throw std::runtime_error("Order not found: " + client_order_id);
    }
    
    const auto& order = *order_opt;
    
    // Fallback: if exchange_order_id is missing, wait briefly for WS update to populate it
    if (!order.exchange_order_id.has_value()) {
        spdlog::warn("[HL] Order {} missing exchange_order_id, waiting for WS update...", client_order_id);
        
        // Wait up to 2 seconds for exchange_order_id to arrive via WS
        for (int retry = 0; retry < 20; ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            auto updated_order = order_tracker_.get_order(client_order_id);
            if (updated_order.has_value() && updated_order->exchange_order_id.has_value()) {
                spdlog::info("[HL] exchange_order_id {} acquired for order {}",
                           *updated_order->exchange_order_id, client_order_id);
                return execute_cancel_order(*updated_order);
            }
        }
        
        // Still missing after timeout - cannot cancel without oid
        spdlog::error("[HL] Timeout waiting for exchange_order_id for order {}", client_order_id);
        throw std::runtime_error("Order has no exchange ID after timeout: " + client_order_id);
    }
    
    // Get asset index
    std::string coin = extract_coin_from_pair(trading_pair);
    int asset_index = coin_to_asset_[coin];
    
    // Build cancel request
    nlohmann::json action = {
        {"type", "cancel"},
        {"cancels", nlohmann::json::array({
            {
                {"a", asset_index},
                {"o", std::stoll(*order.exchange_order_id)}
            }
        })}
    };
    
    // Sign and send
    auto result = api_post_with_auth("/exchange", action);
    
    // Process result
    if (result["status"] == "ok") {
        OrderUpdate update;
        update.client_order_id = client_order_id;
        update.new_state = OrderState::PENDING_CANCEL;
        update.update_timestamp = current_timestamp_ns();
        order_tracker_.process_order_update(update);
        return true;
    }
    
    return false;
}

bool HyperliquidPerpetualConnector::execute_cancel_order(const InFlightOrder& order) {
    // Check if we have exchange_order_id
    if (!order.exchange_order_id.has_value()) {
        spdlog::error("[HL] Cannot cancel order {} without exchange_order_id", order.client_order_id);
        return false;
    }
    
    std::string coin = extract_coin_from_pair(order.trading_pair);
    
    if (coin_to_asset_.find(coin) == coin_to_asset_.end()) {
        throw std::runtime_error("Unknown asset: " + coin);
    }
    int asset_index = coin_to_asset_[coin];
    
    nlohmann::json action = {
        {"type", "cancel"},
        {"cancels", nlohmann::json::array({
            {
                {"a", asset_index},
                {"o", std::stoll(*order.exchange_order_id)}
            }
        })}
    };
    
    try {
        auto result = api_post_with_auth("/exchange", action);
        
        // Check response status
        if (result["status"] == "err") {
            spdlog::error("[HL] Cancel rejected: {}", result.dump());
            return false;
        }
        
        OrderUpdate update;
        update.client_order_id = order.client_order_id;
        update.new_state = OrderState::PENDING_CANCEL;
        update.update_timestamp = current_timestamp_ns();
        order_tracker_.process_order_update(update);
        
        spdlog::info("[HL] Cancel request sent for order {} (oid={})",
                    order.client_order_id, *order.exchange_order_id);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to execute cancel: {}", e.what());
        return false;
    }
}

void HyperliquidPerpetualConnector::handle_user_stream_message(const UserStreamMessage& msg) {
    if (msg.type == UserStreamMessage::Type::TRADE) {
        process_trade_update(msg);
    } else if (msg.type == UserStreamMessage::Type::ORDER_UPDATE) {
        process_order_update(msg);
    }
}

void HyperliquidPerpetualConnector::process_trade_update(const UserStreamMessage& msg) {
    try {
        std::string cloid = msg.data.value("cloid", "");
        auto order_opt = order_tracker_.get_order(cloid);
        
        if (!order_opt) {
            // Try to find by exchange order ID
            int64_t oid = msg.data.value("exchange_order_id", 0);
            if (oid > 0) {
                order_opt = order_tracker_.get_order_by_exchange_id(std::to_string(oid));
            }
        }
        
        if (!order_opt.has_value()) {
            spdlog::warn("Received trade for unknown order: cloid={}", cloid);
            return;
        }
        
        const auto& order = *order_opt;
        
        // Build trade update with liquidity flag
        TradeUpdate trade;
        trade.trade_id = std::to_string(msg.data.value("trade_id", 0));
        trade.client_order_id = order.client_order_id;
        trade.exchange_order_id = order.exchange_order_id.value_or("");
        trade.trading_pair = order.trading_pair;
        trade.fill_price = std::stod(msg.data.value("price", "0"));
        trade.fill_base_amount = std::stod(msg.data.value("size", "0"));
        trade.fill_quote_amount = trade.fill_price * trade.fill_base_amount;
        trade.fee_amount = std::stod(msg.data.value("fee", "0"));
        trade.fee_currency = "USDC";
        trade.fill_timestamp = msg.data.value("time", 0ULL) * 1000000;  // ms to ns
        
        // Extract liquidity flag (maker/taker) if available
        // Hyperliquid may report this in the fill message
        if (msg.data.contains("liquidity")) {
            trade.liquidity = msg.data.value("liquidity", "taker");
        } else {
            // Infer from fee: negative fee = maker rebate
            trade.liquidity = (trade.fee_amount < 0) ? "maker" : "taker";
        }
        
        order_tracker_.process_trade_update(trade);
        
        // Emit fill event with actual data
        if (event_listener_) {
            event_listener_->on_order_filled(
                order.client_order_id,
                trade.fill_price,
                trade.fill_base_amount
            );
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to process trade update: {}", e.what());
    }
}

void HyperliquidPerpetualConnector::process_order_update(const UserStreamMessage& msg) {
    try {
        std::string cloid = msg.data.value("cloid", "");
        auto order_opt = order_tracker_.get_order(cloid);
        
        if (!order_opt.has_value()) {
            // Try to find by exchange order ID if cloid lookup failed
            if (msg.data.contains("exchange_order_id")) {
                int64_t oid = msg.data.value("exchange_order_id", 0);
                if (oid > 0) {
                    order_opt = order_tracker_.get_order_by_exchange_id(std::to_string(oid));
                }
            }
            
            if (!order_opt.has_value()) {
                return;
            }
        }
        
        const auto& order = *order_opt;
        std::string status = msg.data.value("status", "");
        OrderState new_state = OrderState::OPEN;
        
        // Determine state from status and fill progress
        if (status == "filled") {
            new_state = OrderState::FILLED;
        } else if (status == "cancelled" || status == "rejected") {
            new_state = OrderState::CANCELLED;
        } else if (status == "open" || status.empty()) {
            // Check for partial fills using filled_sz/orig_sz
            if (msg.data.contains("filled_sz") && msg.data.contains("orig_sz")) {
                try {
                    double filled_sz = std::stod(msg.data.value("filled_sz", "0"));
                    double orig_sz = std::stod(msg.data.value("orig_sz", "0"));
                    
                    if (filled_sz > 0 && filled_sz < orig_sz) {
                        new_state = OrderState::PARTIALLY_FILLED;
                    } else if (filled_sz >= orig_sz && orig_sz > 0) {
                        new_state = OrderState::FILLED;
                    } else {
                        new_state = OrderState::OPEN;
                    }
                } catch (...) {
                    new_state = OrderState::OPEN;
                }
            } else {
                new_state = OrderState::OPEN;
            }
        }
        
        OrderUpdate update;
        update.client_order_id = order.client_order_id;
        update.new_state = new_state;
        update.update_timestamp = current_timestamp_ns();
        
        // Backfill exchange_order_id from WS update if missing in tracker
        if (!order.exchange_order_id.has_value() && msg.data.contains("exchange_order_id")) {
            int64_t oid = msg.data.value("exchange_order_id", 0);
            if (oid > 0) {
                update.exchange_order_id = std::to_string(oid);
                spdlog::debug("[HL] Backfilled exchange_order_id {} for order {}", 
                             *update.exchange_order_id, order.client_order_id);
            }
        }
        
        order_tracker_.process_order_update(update);
        
        // Emit events (don't emit zeroed fills - actual fills come via process_trade_update)
        if (new_state == OrderState::FILLED && event_listener_) {
            // Get accumulated fill info from tracker
            auto updated_order = order_tracker_.get_order(order.client_order_id);
            if (updated_order.has_value()) {
                event_listener_->on_order_completed(
                    order.client_order_id,
                    updated_order->average_fill_price,
                    updated_order->filled_amount
                );
            }
        } else if (new_state == OrderState::CANCELLED && event_listener_) {
            event_listener_->on_order_cancelled(order.client_order_id);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to process order update: {}", e.what());
    }
}

nlohmann::json HyperliquidPerpetualConnector::api_post_with_auth(
    const std::string& endpoint,
    const nlohmann::json& action) {
    
    // Sign the action - this returns the complete request with action, nonce, and signature
    auto signed_request = auth_->sign_l1_action(action, !testnet_);
    
    // Log the full request for debugging (use info level to ensure it shows)
    spdlog::info("[HL-REST] Request to {}: {}", endpoint, signed_request.dump());
    
    return rest_post(endpoint, signed_request);
}

nlohmann::json HyperliquidPerpetualConnector::rest_post(
    const std::string& endpoint,
    const nlohmann::json& data) {
    
    net::io_context ioc;
    ssl::context ctx(ssl::context::tlsv12_client);
    ctx.set_default_verify_paths();
    
    beast::ssl_stream<tcp::socket> stream(ioc, ctx);
    
    // Set SNI hostname for proper SSL verification
    if (!SSL_set_tlsext_host_name(stream.native_handle(), rest_host_.c_str())) {
        throw std::runtime_error("Failed to set SNI hostname for REST request");
    }
    
    tcp::resolver resolver(ioc);
    auto const results = resolver.resolve(rest_host_, "443");
    net::connect(stream.next_layer(), results);
    
    stream.handshake(ssl::stream_base::client);
    
    http::request<http::string_body> req{http::verb::post, endpoint, 11};
    req.set(http::field::host, rest_host_);
    req.set(http::field::content_type, "application/json");
    req.body() = data.dump();
    req.prepare_payload();
    
    http::write(stream, req);
    
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(stream, buffer, res);
    
    beast::error_code ec;
    stream.shutdown(ec);
    
    // Log response details (use info for status, error for failures)
    spdlog::info("[HL-REST] Response status: {} {}", res.result_int(), res.reason());
    
    // Check HTTP status code
    if (res.result() != http::status::ok) {
        // Log full response body for debugging
        spdlog::error("[HL-REST] Response body: {}", res.body());
        
        std::string error_msg = "HTTP " + std::to_string(res.result_int()) + 
                               " " + std::string(res.reason()) + 
                               ": " + res.body();
        spdlog::error("[HL-REST] Request failed: {}", error_msg);
        throw std::runtime_error(error_msg);
    }
    
    // Log successful response body
    spdlog::info("[HL-REST] Response body: {}", res.body().substr(0, std::min(size_t(1000), res.body().size())));
    
    // Try to parse JSON response
    try {
        return nlohmann::json::parse(res.body());
    } catch (const nlohmann::json::parse_error& e) {
        std::string error_msg = "Failed to parse JSON response: " + std::string(e.what()) + 
                               "\nResponse body: " + res.body();
        spdlog::error("[HL-REST] {}", error_msg);
        throw std::runtime_error(error_msg);
    }
}

void HyperliquidPerpetualConnector::fetch_trading_rules() {
    nlohmann::json request = {{"type", "meta"}};
    auto response = rest_post("/info", request);
    
    if (response.contains("universe")) {
        for (size_t i = 0; i < response["universe"].size(); ++i) {
            const auto& asset = response["universe"][i];
            std::string name = asset["name"];
            
            coin_to_asset_[name] = static_cast<int>(i);
            
            std::string trading_pair = name + "-USD";
            int sz_decimals = asset.value("szDecimals", 3);
            
            // Extract price decimals from maxLeverage or default to 5 sig figs (Hyperliquid default)
            // Hyperliquid uses 5 significant figures for most assets
            int price_decimals = 5;  // Default for Hyperliquid
            
            TradingRule rule;
            rule.trading_pair = trading_pair;
            rule.min_order_size = 0.0;  // Hyperliquid has dynamic minimums based on notional
            rule.max_order_size = 1000000.0;
            rule.tick_size = std::pow(10.0, -price_decimals);
            rule.step_size = std::pow(10.0, -sz_decimals);
            rule.price_decimals = price_decimals;
            rule.size_decimals = sz_decimals;
            trading_rules_[trading_pair] = rule;
            
            spdlog::debug("[HL] Trading rule for {}: price_decimals={}, sz_decimals={}, tick_size={}, step_size={}",
                         trading_pair, price_decimals, sz_decimals, rule.tick_size, rule.step_size);
        }
        
        spdlog::info("Fetched trading rules for {} pairs", trading_rules_.size());
    }
}

bool HyperliquidPerpetualConnector::validate_order_params(const OrderParams& params) const {
    if (params.trading_pair.empty()) return false;
    if (params.amount <= 0) return false;
    if (params.order_type == OrderType::LIMIT && params.price <= 0) return false;
    return true;
}

std::string HyperliquidPerpetualConnector::extract_coin_from_pair(const std::string& trading_pair) const {
    size_t pos = trading_pair.find('-');
    if (pos != std::string::npos) {
        return trading_pair.substr(0, pos);
    }
    return trading_pair;
}

void HyperliquidPerpetualConnector::emit_order_created_event(
    const std::string& client_order_id,
    const std::string& exchange_order_id) {
    if (event_listener_) {
        event_listener_->on_order_created(client_order_id, exchange_order_id);
    }
}

void HyperliquidPerpetualConnector::emit_order_failure_event(
    const std::string& client_order_id,
    const std::string& reason) {
    if (event_listener_) {
        event_listener_->on_order_failed(client_order_id, reason);
    }
}

} // namespace latentspeed::connector
