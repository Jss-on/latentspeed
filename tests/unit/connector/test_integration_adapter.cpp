#include <gtest/gtest.h>
#include "connector/hyperliquid_marketstream_adapter.h"
#include "exchange_interface.h"
#include <memory>

using namespace latentspeed::connector;

/**
 * @brief Mock HyperliquidExchange for testing
 */
class MockHyperliquidExchange : public HyperliquidExchange {
public:
    MockHyperliquidExchange() : connected_(false) {}
    
    bool is_connected() const override {
        return connected_;
    }
    
    void set_connected(bool connected) {
        connected_ = connected;
    }
    
    // Mock methods
    bool initialize() override { return true; }
    void start() override { connected_ = true; }
    void stop() override { connected_ = false; }
    
    void subscribe_orderbook(const std::string& coin) override {
        subscribed_coins_.insert(coin);
    }
    
    void unsubscribe_orderbook(const std::string& coin) override {
        subscribed_coins_.erase(coin);
    }
    
    std::vector<std::string> get_available_pairs() const override {
        return {"BTC-USD", "ETH-USD", "SOL-USD"};
    }
    
    const std::set<std::string>& get_subscribed_coins() const {
        return subscribed_coins_;
    }
    
private:
    bool connected_;
    std::set<std::string> subscribed_coins_;
};

/**
 * @brief Test fixture for marketstream adapter
 */
class MarketstreamAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_exchange = std::make_shared<MockHyperliquidExchange>();
    }
    
    std::shared_ptr<MockHyperliquidExchange> mock_exchange;
};

// ============================================================================
// TESTS
// ============================================================================

TEST_F(MarketstreamAdapterTest, ConstructorRequiresNonNullExchange) {
    // Should throw if exchange is null
    EXPECT_THROW({
        HyperliquidMarketstreamAdapter adapter(nullptr);
    }, std::invalid_argument);
}

TEST_F(MarketstreamAdapterTest, ConstructorAcceptsValidExchange) {
    // Should construct successfully with valid exchange
    EXPECT_NO_THROW({
        HyperliquidMarketstreamAdapter adapter(mock_exchange);
    });
}

TEST_F(MarketstreamAdapterTest, InitializeSucceeds) {
    HyperliquidMarketstreamAdapter adapter(mock_exchange);
    
    EXPECT_TRUE(adapter.initialize());
}

TEST_F(MarketstreamAdapterTest, ConnectedStateReflectsExchange) {
    HyperliquidMarketstreamAdapter adapter(mock_exchange);
    
    // Initially not connected
    EXPECT_FALSE(adapter.is_connected());
    
    // Connect the mock exchange
    mock_exchange->set_connected(true);
    EXPECT_TRUE(adapter.is_connected());
    
    // Disconnect
    mock_exchange->set_connected(false);
    EXPECT_FALSE(adapter.is_connected());
}

TEST_F(MarketstreamAdapterTest, SubscribeOrderbookForwardsToCoin) {
    HyperliquidMarketstreamAdapter adapter(mock_exchange);
    adapter.initialize();
    
    // Subscribe to BTC-USD
    adapter.subscribe_orderbook("BTC-USD");
    
    // Should have subscribed to "BTC" coin
    const auto& subscribed = mock_exchange->get_subscribed_coins();
    EXPECT_TRUE(subscribed.count("BTC") > 0);
}

TEST_F(MarketstreamAdapterTest, UnsubscribeOrderbookWorks) {
    HyperliquidMarketstreamAdapter adapter(mock_exchange);
    adapter.initialize();
    
    // Subscribe then unsubscribe
    adapter.subscribe_orderbook("ETH-USD");
    adapter.unsubscribe_orderbook("ETH-USD");
    
    // Should not be subscribed
    const auto& subscribed = mock_exchange->get_subscribed_coins();
    EXPECT_FALSE(subscribed.count("ETH") > 0);
}

TEST_F(MarketstreamAdapterTest, GetTradingPairsReturnsExchangePairs) {
    HyperliquidMarketstreamAdapter adapter(mock_exchange);
    
    auto pairs = adapter.get_trading_pairs();
    
    EXPECT_EQ(pairs.size(), 3);
    EXPECT_TRUE(std::find(pairs.begin(), pairs.end(), "BTC-USD") != pairs.end());
    EXPECT_TRUE(std::find(pairs.begin(), pairs.end(), "ETH-USD") != pairs.end());
    EXPECT_TRUE(std::find(pairs.begin(), pairs.end(), "SOL-USD") != pairs.end());
}

TEST_F(MarketstreamAdapterTest, ConnectorNameIsCorrect) {
    HyperliquidMarketstreamAdapter adapter(mock_exchange);
    
    EXPECT_EQ(adapter.connector_name(), "hyperliquid_marketstream_adapter");
}

TEST_F(MarketstreamAdapterTest, SymbolNormalizationRemovesSuffix) {
    HyperliquidMarketstreamAdapter adapter(mock_exchange);
    adapter.initialize();
    
    // Test various formats
    adapter.subscribe_orderbook("BTC-USD");
    adapter.subscribe_orderbook("ETH-USDT");
    adapter.subscribe_orderbook("SOL-PERP");
    
    const auto& subscribed = mock_exchange->get_subscribed_coins();
    
    // All should be normalized to coin name only
    EXPECT_TRUE(subscribed.count("BTC") > 0);
    EXPECT_TRUE(subscribed.count("ETH") > 0);
    EXPECT_TRUE(subscribed.count("SOL") > 0);
}

TEST_F(MarketstreamAdapterTest, StartAndStopDoNotAffectExchange) {
    HyperliquidMarketstreamAdapter adapter(mock_exchange);
    adapter.initialize();
    
    // Start should not change exchange state (it's already running)
    bool initial_state = mock_exchange->is_connected();
    adapter.start();
    EXPECT_EQ(mock_exchange->is_connected(), initial_state);
    
    // Stop should not stop the exchange (other components may be using it)
    mock_exchange->set_connected(true);
    adapter.stop();
    EXPECT_TRUE(mock_exchange->is_connected());  // Still connected
}

TEST_F(MarketstreamAdapterTest, HandleNullExchangeGracefully) {
    // Create adapter with valid exchange, then test edge cases
    HyperliquidMarketstreamAdapter adapter(mock_exchange);
    
    // These should not crash even with edge cases
    EXPECT_NO_THROW(adapter.subscribe_orderbook(""));
    EXPECT_NO_THROW(adapter.unsubscribe_orderbook("INVALID"));
}
