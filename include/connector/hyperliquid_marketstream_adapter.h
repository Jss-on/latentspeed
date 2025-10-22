#pragma once

#include "connector/order_book_tracker_data_source.h"
#include "exchange_interface.h"  // Your existing HyperliquidExchange
#include <memory>
#include <string>
#include <optional>
#include <spdlog/spdlog.h>

namespace latentspeed {

/**
 * @brief Adapter that wraps existing HyperliquidExchange (marketstream) to implement
 *        OrderBookTrackerDataSource interface from Phase 3.
 * 
 * This allows us to reuse your battle-tested marketstream implementation
 * while integrating with the new connector architecture.
 */
class HyperliquidMarketstreamAdapter : public OrderBookTrackerDataSource {
public:
    /**
     * @brief Construct adapter with existing HyperliquidExchange
     * @param exchange Your existing marketstream Hyperliquid exchange
     */
    explicit HyperliquidMarketstreamAdapter(std::shared_ptr<HyperliquidExchange> exchange)
        : exchange_(exchange) {
        if (!exchange_) {
            throw std::invalid_argument("HyperliquidExchange cannot be null");
        }
    }

    ~HyperliquidMarketstreamAdapter() override = default;

    // Implement OrderBookTrackerDataSource interface
    
    bool initialize() override {
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

    void start() override {
        // Your marketstream is already running
        // Nothing to start here
        spdlog::info("HyperliquidMarketstreamAdapter: Marketstream already running");
    }

    void stop() override {
        // Managed by your existing marketstream system
        // We don't stop it here (other parts of your system may use it)
        spdlog::info("HyperliquidMarketstreamAdapter: Leaving marketstream running");
    }

    bool is_connected() const override {
        return exchange_ && exchange_->is_connected();
    }

    void subscribe_orderbook(const std::string& trading_pair) override {
        if (!exchange_) {
            spdlog::error("Cannot subscribe: exchange is null");
            return;
        }
        
        // Convert trading_pair to your format
        // e.g., "BTC-USD" -> "BTC"
        std::string coin = normalize_symbol(trading_pair);
        
        spdlog::info("HyperliquidMarketstreamAdapter: Subscribing to orderbook for {}", coin);
        
        // Subscribe via your existing marketstream
        exchange_->subscribe_orderbook(coin);
    }

    void unsubscribe_orderbook(const std::string& trading_pair) override {
        if (!exchange_) {
            return;
        }
        
        std::string coin = normalize_symbol(trading_pair);
        spdlog::info("HyperliquidMarketstreamAdapter: Unsubscribing from orderbook for {}", coin);
        exchange_->unsubscribe_orderbook(coin);
    }

    std::optional<OrderBook> get_snapshot(const std::string& trading_pair) override {
        if (!exchange_) {
            return std::nullopt;
        }
        
        // Get snapshot from your existing orderbook cache
        // You may need to adapt this based on your actual HyperliquidExchange API
        auto snapshot = exchange_->get_orderbook_snapshot(trading_pair);
        
        if (!snapshot) {
            return std::nullopt;
        }
        
        // Convert your snapshot format to our OrderBook format
        OrderBook orderbook(trading_pair);
        
        // Assuming your snapshot has bids/asks
        for (const auto& [price, size] : snapshot->bids) {
            orderbook.apply_bid(price, size);
        }
        
        for (const auto& [price, size] : snapshot->asks) {
            orderbook.apply_ask(price, size);
        }
        
        return orderbook;
    }

    std::vector<std::string> get_trading_pairs() const override {
        // Return trading pairs available in your marketstream
        if (!exchange_) {
            return {};
        }
        return exchange_->get_available_pairs();
    }

    std::string connector_name() const override {
        return "hyperliquid_marketstream_adapter";
    }

private:
    /**
     * @brief Set up forwarding from your marketstream callbacks to our interface
     */
    void setup_message_forwarding() {
        if (!exchange_) {
            return;
        }
        
        // Hook into your existing marketstream callbacks
        // Adapt this based on your actual HyperliquidExchange callback API
        
        exchange_->set_orderbook_callback([this](const auto& data) {
            // Convert your orderbook update to our OrderBookMessage format
            OrderBookMessage msg;
            msg.type = OrderBookMessageType::SNAPSHOT;  // or DIFF based on your data
            msg.trading_pair = data.symbol;
            msg.timestamp = data.timestamp;
            
            // Convert bids/asks
            for (const auto& [price, size] : data.bids) {
                msg.bids.emplace_back(price, size);
            }
            
            for (const auto& [price, size] : data.asks) {
                msg.asks.emplace_back(price, size);
            }
            
            // Emit to our subscribers (Phase 3 callback mechanism)
            emit_message(msg);
        });
        
        spdlog::info("HyperliquidMarketstreamAdapter: Message forwarding configured");
    }

    /**
     * @brief Normalize trading pair symbol
     * @param trading_pair e.g., "BTC-USD", "ETH-USDT"
     * @return Normalized symbol e.g., "BTC", "ETH"
     */
    std::string normalize_symbol(const std::string& trading_pair) const {
        // Remove common suffixes: -USD, -USDT, -PERP
        std::string symbol = trading_pair;
        
        // Find hyphen
        size_t pos = symbol.find('-');
        if (pos != std::string::npos) {
            symbol = symbol.substr(0, pos);
        }
        
        return symbol;
    }

    std::shared_ptr<HyperliquidExchange> exchange_;
};

}  // namespace latentspeed
