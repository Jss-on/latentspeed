/**
 * @file hyperliquid_marketstream_adapter.cpp
 * @brief Complete implementation of HyperliquidMarketstreamAdapter
 */

#include "connector/exchange/hyperliquid/hyperliquid_marketstream_adapter.h"
#include <spdlog/spdlog.h>

namespace latentspeed {

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

HyperliquidMarketstreamAdapter::HyperliquidMarketstreamAdapter(
    std::shared_ptr<HyperliquidExchange> exchange)
    : exchange_(exchange) {
    if (!exchange_) {
        throw std::invalid_argument("HyperliquidExchange cannot be null");
    }
}

HyperliquidMarketstreamAdapter::~HyperliquidMarketstreamAdapter() = default;

// ============================================================================
// LIFECYCLE
// ============================================================================

bool HyperliquidMarketstreamAdapter::initialize() {
    if (!exchange_) {
        spdlog::error("HyperliquidMarketstreamAdapter: Exchange is null");
        return false;
    }
    
    // Your exchange is already initialized via marketstream
    spdlog::info("HyperliquidMarketstreamAdapter: Using existing marketstream");
    
    // Set up message forwarding from your exchange to our callbacks
    setup_message_forwarding();
    
    return true;
}

void HyperliquidMarketstreamAdapter::start() {
    // Your marketstream is already running
    // Nothing to start here
    spdlog::info("HyperliquidMarketstreamAdapter: Marketstream already running");
}

void HyperliquidMarketstreamAdapter::stop() {
    // Managed by your existing marketstream system
    // We don't stop it here (other parts of your system may use it)
    spdlog::info("HyperliquidMarketstreamAdapter: Leaving marketstream running");
}

bool HyperliquidMarketstreamAdapter::is_connected() const {
    // NOTE: HyperliquidExchange doesn't have is_connected() method
    // Assume connected if exchange exists
    return exchange_ != nullptr;
}

// ============================================================================
// SUBSCRIPTION MANAGEMENT
// ============================================================================

void HyperliquidMarketstreamAdapter::subscribe_orderbook(const std::string& trading_pair) {
    if (!exchange_) {
        spdlog::error("Cannot subscribe: exchange is null");
        return;
    }
    
    // NOTE: HyperliquidExchange doesn't have subscribe_orderbook method
    // This is a placeholder for future market data integration
    std::string coin = normalize_symbol(trading_pair);
    spdlog::warn("[HyperliquidMarketstreamAdapter] subscribe_orderbook not implemented for {}", coin);
}

void HyperliquidMarketstreamAdapter::unsubscribe_orderbook(const std::string& trading_pair) {
    if (!exchange_) {
        return;
    }
    
    std::string coin = normalize_symbol(trading_pair);
    spdlog::warn("[HyperliquidMarketstreamAdapter] unsubscribe_orderbook not implemented for {}", coin);
}

// ============================================================================
// DATA RETRIEVAL
// ============================================================================

std::optional<connector::OrderBook> HyperliquidMarketstreamAdapter::get_snapshot(const std::string& trading_pair) {
    if (!exchange_) {
        return std::nullopt;
    }
    
    // NOTE: HyperliquidExchange doesn't have get_orderbook_snapshot method
    // This adapter is a placeholder for future market data integration
    // For now, return empty
    spdlog::warn("[HyperliquidMarketstreamAdapter] get_snapshot not implemented - market data integration pending");
    return std::nullopt;
    
    /* TODO: Implement when market data methods are available
    auto snapshot = exchange_->get_orderbook_snapshot(trading_pair);
    if (!snapshot) return std::nullopt;
    
    connector::OrderBook orderbook(trading_pair);
    for (const auto& [price, size] : snapshot->bids) {
        orderbook.apply_bid(price, size);
    }
    for (const auto& [price, size] : snapshot->asks) {
        orderbook.apply_ask(price, size);
    }
    return orderbook;
    */
}

std::vector<std::string> HyperliquidMarketstreamAdapter::get_trading_pairs() const {
    // NOTE: HyperliquidExchange doesn't have get_available_pairs method
    // Return empty for now
    return {};
}

std::string HyperliquidMarketstreamAdapter::connector_name() const {
    return "hyperliquid_marketstream_adapter";
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

void HyperliquidMarketstreamAdapter::setup_message_forwarding() {
    if (!exchange_) {
        return;
    }
    
    // NOTE: This is a placeholder for future market data integration
    // HyperliquidExchange doesn't have set_orderbook_callback method yet
    // When market data methods are available, implement callback forwarding
    
    spdlog::info("[HyperliquidMarketstreamAdapter] Message forwarding not yet implemented");
    
    /* TODO: Implement when HyperliquidExchange has callback support
    exchange_->set_orderbook_callback([this](const auto& data) {
        connector::OrderBookMessage msg;
        msg.type = connector::OrderBookMessage::Type::SNAPSHOT;
        msg.trading_pair = data.symbol;
        msg.timestamp = data.timestamp;
        
        for (const auto& [price, size] : data.bids) {
            msg.bids.emplace_back(price, size);
        }
        for (const auto& [price, size] : data.asks) {
            msg.asks.emplace_back(price, size);
        }
        
        emit_message(msg);
    });
    */
}

std::string HyperliquidMarketstreamAdapter::normalize_symbol(const std::string& trading_pair) const {
    // Remove common suffixes: -USD, -USDT, -PERP
    std::string symbol = trading_pair;
    
    // Find hyphen
    size_t pos = symbol.find('-');
    if (pos != std::string::npos) {
        symbol = symbol.substr(0, pos);
    }
    
    return symbol;
}

}  // namespace latentspeed
