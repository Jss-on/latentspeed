#pragma once

#include "connector/order_book_tracker_data_source.h"
#include "marketstream/exchange_interface.h"  // Your existing HyperliquidExchange
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
class HyperliquidMarketstreamAdapter : public connector::OrderBookTrackerDataSource {
public:
    /**
     * @brief Construct adapter with existing HyperliquidExchange
     * @param exchange Your existing marketstream Hyperliquid exchange
     */
    explicit HyperliquidMarketstreamAdapter(std::shared_ptr<HyperliquidExchange> exchange);

    ~HyperliquidMarketstreamAdapter() override;

    // Implement OrderBookTrackerDataSource interface
    
    bool initialize() override;

    void start() override;

    void stop() override;

    bool is_connected() const override;

    void subscribe_orderbook(const std::string& trading_pair) override;

    void unsubscribe_orderbook(const std::string& trading_pair) override;

    std::optional<connector::OrderBook> get_snapshot(const std::string& trading_pair) override;

    // Additional methods (not from base class)
    std::vector<std::string> get_trading_pairs() const;

    std::string connector_name() const;

private:
    /**
     * @brief Set up forwarding from your marketstream callbacks to our interface
     */
    void setup_message_forwarding();

    /**
     * @brief Normalize trading pair symbol
     * @param trading_pair e.g., "BTC-USD", "ETH-USDT"
     * @return Normalized symbol e.g., "BTC", "ETH"
     */
    std::string normalize_symbol(const std::string& trading_pair) const;

    std::shared_ptr<HyperliquidExchange> exchange_;
};

}  // namespace latentspeed
