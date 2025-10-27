/**
 * @file hyperliquid_user_stream_data_source.cpp
 * @brief Implementation of HyperliquidUserStreamDataSource
 */

#include "connector/exchange/hyperliquid/hyperliquid_user_stream_data_source.h"
#include <spdlog/spdlog.h>

namespace latentspeed::connector {

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

HyperliquidUserStreamDataSource::HyperliquidUserStreamDataSource(std::shared_ptr<HyperliquidAuth> auth)
    : auth_(auth),
      io_context_(),
      ssl_context_(ssl::context::tlsv12_client),
      ws_(nullptr),
      resolver_(io_context_),
      running_(false),
      connected_(false) {
    
    // Configure SSL context
    ssl_context_.set_default_verify_paths();
    ssl_context_.set_verify_mode(ssl::verify_peer);
}

HyperliquidUserStreamDataSource::~HyperliquidUserStreamDataSource() {
    stop();
}

// ============================================================================
// LIFECYCLE
// ============================================================================

bool HyperliquidUserStreamDataSource::initialize() {
    if (!auth_ || auth_->get_address().empty()) {
        spdlog::error("HyperliquidUserStreamDataSource requires valid auth");
        return false;
    }
    return true;
}

void HyperliquidUserStreamDataSource::start() {
    if (running_) return;
    
    running_ = true;
    ws_thread_ = std::thread([this]() { run_websocket(); });
    spdlog::info("HyperliquidUserStreamDataSource started");
}

void HyperliquidUserStreamDataSource::stop() {
    if (!running_) return;
    
    running_ = false;
    
    // Close WebSocket
    if (ws_ && ws_->is_open()) {
        try {
            ws_->close(websocket::close_code::normal);
        } catch (...) {
            // Ignore errors on shutdown
        }
    }
    
    io_context_.stop();
    
    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
    
    connected_ = false;
    spdlog::info("HyperliquidUserStreamDataSource stopped");
}

bool HyperliquidUserStreamDataSource::is_connected() const {
    return connected_;
}

// ============================================================================
// SUBSCRIPTION MANAGEMENT
// ============================================================================

void HyperliquidUserStreamDataSource::subscribe_to_order_updates() {
    subscribed_to_orders_ = true;
    if (ws_ && ws_->is_open()) {
        send_user_subscription();
    }
}

void HyperliquidUserStreamDataSource::subscribe_to_balance_updates() {
    // Hyperliquid sends balance updates in the same user channel
    subscribed_to_balances_ = true;
}

void HyperliquidUserStreamDataSource::subscribe_to_position_updates() {
    // Hyperliquid sends position updates in the same user channel
    subscribed_to_positions_ = true;
}

// ============================================================================
// WEBSOCKET MANAGEMENT (Private)
// ============================================================================

void HyperliquidUserStreamDataSource::run_websocket() {
    while (running_) {
        try {
            connect_websocket();
            
            if (subscribed_to_orders_) {
                send_user_subscription();
            }
            
            read_messages();
            
        } catch (const std::exception& e) {
            spdlog::error("User stream WebSocket error: {}", e.what());
            connected_ = false;
            
            if (running_) {
                spdlog::info("Reconnecting user stream in 5 seconds...");
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    }
}

void HyperliquidUserStreamDataSource::connect_websocket() {
    // Resolve hostname
    auto const results = resolver_.resolve(WS_URL, WS_PORT);
    
    // Create WebSocket stream with SSL
    ws_ = std::make_unique<websocket::stream<beast::ssl_stream<tcp::socket>>>(
        io_context_, ssl_context_
    );
    
    // Set SNI hostname
    if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), WS_URL)) {
        throw std::runtime_error("Failed to set SNI hostname");
    }
    
    // Connect
    auto ep = net::connect(ws_->next_layer().next_layer(), results);
    
    // SSL handshake
    ws_->next_layer().handshake(ssl::stream_base::client);
    
    // WebSocket handshake
    ws_->handshake(WS_URL, WS_PATH);
    
    connected_ = true;
    spdlog::info("Connected to Hyperliquid user stream WebSocket");
}

void HyperliquidUserStreamDataSource::read_messages() {
    while (running_ && ws_ && ws_->is_open()) {
        beast::flat_buffer buffer;
        ws_->read(buffer);
        
        std::string message = beast::buffers_to_string(buffer.data());
        process_message(message);
    }
}

void HyperliquidUserStreamDataSource::process_message(const std::string& message) {
    try {
        auto json = nlohmann::json::parse(message);
        
        if (json.contains("channel")) {
            std::string channel = json["channel"];
            
            if (channel == "user") {
                process_user_update(json["data"]);
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to process user stream message: {}", e.what());
    }
}

void HyperliquidUserStreamDataSource::process_user_update(const nlohmann::json& data) {
    // Hyperliquid user stream includes multiple update types
    
    // Process fills
    if (data.contains("fills")) {
        for (const auto& fill : data["fills"]) {
            process_fill(fill);
        }
    }
    
    // Process order updates
    if (data.contains("orders")) {
        for (const auto& order : data["orders"]) {
            process_order_update(order);
        }
    }
    
    // Process funding updates
    if (data.contains("funding")) {
        for (const auto& funding : data["funding"]) {
            process_funding_update(funding);
        }
    }
    
    // Process liquidations
    if (data.contains("liquidations")) {
        for (const auto& liq : data["liquidations"]) {
            process_liquidation(liq);
        }
    }
    
    // Process non-funding ledger updates (withdrawals, deposits, etc.)
    if (data.contains("nonFundingLedgerUpdates")) {
        for (const auto& update : data["nonFundingLedgerUpdates"]) {
            process_ledger_update(update);
        }
    }
}

void HyperliquidUserStreamDataSource::process_fill(const nlohmann::json& fill) {
    UserStreamMessage msg;
    msg.type = UserStreamMessage::Type::TRADE;
    msg.timestamp = current_timestamp_ns();
    
    // Parse fill data
    msg.data = {
        {"trade_id", fill.value("tid", 0)},
        {"exchange_order_id", fill.value("oid", 0)},
        {"price", fill.value("px", "0")},
        {"size", fill.value("sz", "0")},
        {"side", fill.value("side", "")},
        {"fee", fill.value("fee", "0")},
        {"time", fill.value("time", 0)},
        {"cloid", fill.value("cloid", "")}
    };
    
    emit_message(msg);
}

void HyperliquidUserStreamDataSource::process_order_update(const nlohmann::json& order) {
    UserStreamMessage msg;
    msg.type = UserStreamMessage::Type::ORDER_UPDATE;
    msg.timestamp = current_timestamp_ns();
    
    // Parse order data
    msg.data = {
        {"exchange_order_id", order.value("oid", 0)},
        {"coin", order.value("coin", "")},
        {"side", order.value("side", "")},
        {"limit_px", order.value("limitPx", "0")},
        {"sz", order.value("sz", "0")},
        {"timestamp", order.value("timestamp", 0)},
        {"cloid", order.value("cloid", "")}
    };
    
    // Add order status if available
    if (order.contains("order")) {
        const auto& order_info = order["order"];
        msg.data["status"] = order_info.value("status", "");
        msg.data["filled_sz"] = order_info.value("filledSz", "0");
        msg.data["orig_sz"] = order_info.value("origSz", "0");
    }
    
    emit_message(msg);
}

void HyperliquidUserStreamDataSource::process_funding_update(const nlohmann::json& funding) {
    UserStreamMessage msg;
    msg.type = UserStreamMessage::Type::BALANCE_UPDATE;
    msg.timestamp = current_timestamp_ns();
    
    msg.data = {
        {"type", "funding"},
        {"coin", funding.value("coin", "")},
        {"funding_rate", funding.value("fundingRate", "0")},
        {"szi", funding.value("szi", "0")},
        {"usdc", funding.value("usdc", "0")},
        {"time", funding.value("time", 0)}
    };
    
    emit_message(msg);
}

void HyperliquidUserStreamDataSource::process_ledger_update(const nlohmann::json& update) {
    UserStreamMessage msg;
    msg.type = UserStreamMessage::Type::BALANCE_UPDATE;
    msg.timestamp = current_timestamp_ns();
    
    msg.data = {
        {"type", "ledger_update"},
        {"time", update.value("time", 0)},
        {"hash", update.value("hash", "")},
        {"delta", update.value("delta", nlohmann::json::object())}
    };
    
    emit_message(msg);
}

void HyperliquidUserStreamDataSource::process_liquidation(const nlohmann::json& liquidation) {
    UserStreamMessage msg;
    msg.type = UserStreamMessage::Type::ORDER_UPDATE;
    msg.timestamp = current_timestamp_ns();
    
    msg.data = {
        {"type", "liquidation"},
        {"lid", liquidation.value("lid", 0)},
        {"liquidator", liquidation.value("liquidator", "")},
        {"time", liquidation.value("time", 0)}
    };
    
    spdlog::warn("Liquidation event: {}", liquidation.dump());
    emit_message(msg);
}

void HyperliquidUserStreamDataSource::send_user_subscription() {
    if (!auth_) return;
    
    nlohmann::json sub = {
        {"method", "subscribe"},
        {"subscription", {
            {"type", "user"},
            {"user", auth_->get_address()}
        }}
    };
    
    ws_->write(net::buffer(sub.dump()));
    spdlog::info("Subscribed to user stream for address: {}", auth_->get_address());
}

// ============================================================================
// UTILITIES
// ============================================================================

uint64_t HyperliquidUserStreamDataSource::current_timestamp_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // namespace latentspeed::connector
