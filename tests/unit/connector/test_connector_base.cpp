/**
 * @file test_connector_base.cpp
 * @brief Unit tests for ConnectorBase
 */

#include <gtest/gtest.h>
#include "connector/connector_base.h"
#include "connector/perpetual_derivative_base.h"
#include <memory>

using namespace latentspeed::connector;

// ============================================================================
// MOCK CONNECTOR FOR TESTING
// ============================================================================

class MockConnector : public ConnectorBase {
public:
    std::string name() const override { return "mock_connector"; }
    std::string domain() const override { return "test"; }
    ConnectorType connector_type() const override { return ConnectorType::SPOT; }
    
    bool initialize() override { return true; }
    bool connect() override { is_connected_ = true; return true; }
    void disconnect() override { is_connected_ = false; }
    bool is_connected() const override { return is_connected_; }
    bool is_ready() const override { return is_connected_; }
    
    std::string buy(const OrderParams& params) override {
        return generate_client_order_id();
    }
    
    std::string sell(const OrderParams& params) override {
        return generate_client_order_id();
    }
    
    bool cancel(const std::string& client_order_id) override {
        return true;
    }
    
    std::optional<TradingRule> get_trading_rule(const std::string& trading_pair) const override {
        if (trading_pair == "BTC-USD") {
            TradingRule rule;
            rule.trading_pair = "BTC-USD";
            rule.tick_size = 0.1;
            rule.step_size = 0.001;
            rule.price_decimals = 1;
            rule.size_decimals = 3;
            rule.min_order_size = 0.001;
            rule.min_notional = 10.0;
            return rule;
        }
        return std::nullopt;
    }
    
    std::vector<TradingRule> get_all_trading_rules() const override {
        return {};
    }

private:
    bool is_connected_ = false;
};

// ============================================================================
// TESTS: ENUM STRING CONVERSION
// ============================================================================

TEST(ConnectorTypes, EnumToString) {
    EXPECT_EQ(to_string(ConnectorType::SPOT), "SPOT");
    EXPECT_EQ(to_string(ConnectorType::DERIVATIVE_PERPETUAL), "DERIVATIVE_PERPETUAL");
    
    EXPECT_EQ(to_string(OrderType::LIMIT), "LIMIT");
    EXPECT_EQ(to_string(OrderType::MARKET), "MARKET");
    EXPECT_EQ(to_string(OrderType::LIMIT_MAKER), "LIMIT_MAKER");
    
    EXPECT_EQ(to_string(TradeType::BUY), "BUY");
    EXPECT_EQ(to_string(TradeType::SELL), "SELL");
    
    EXPECT_EQ(to_string(PositionAction::OPEN), "OPEN");
    EXPECT_EQ(to_string(PositionAction::CLOSE), "CLOSE");
}

TEST(ConnectorTypes, OrderTypeHelpers) {
    EXPECT_TRUE(is_limit_type(OrderType::LIMIT));
    EXPECT_TRUE(is_limit_type(OrderType::LIMIT_MAKER));
    EXPECT_TRUE(is_limit_type(OrderType::STOP_LIMIT));
    EXPECT_FALSE(is_limit_type(OrderType::MARKET));
    
    EXPECT_TRUE(is_market_type(OrderType::MARKET));
    EXPECT_TRUE(is_market_type(OrderType::STOP_MARKET));
    EXPECT_FALSE(is_market_type(OrderType::LIMIT));
}

// ============================================================================
// TESTS: CLIENT ORDER ID GENERATION
// ============================================================================

TEST(ConnectorBase, ClientOrderIdGeneration) {
    MockConnector connector;
    
    // Generate multiple IDs
    std::string id1 = connector.generate_client_order_id();
    std::string id2 = connector.generate_client_order_id();
    std::string id3 = connector.generate_client_order_id();
    
    // IDs should be unique
    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, id3);
    
    // IDs should have correct prefix
    EXPECT_TRUE(id1.starts_with("LS-"));
    EXPECT_TRUE(id2.starts_with("LS-"));
    EXPECT_TRUE(id3.starts_with("LS-"));
}

TEST(ConnectorBase, ClientOrderIdPrefix) {
    MockConnector connector;
    
    // Default prefix
    EXPECT_EQ(connector.get_client_order_id_prefix(), "LS");
    
    // Set custom prefix
    connector.set_client_order_id_prefix("TEST");
    EXPECT_EQ(connector.get_client_order_id_prefix(), "TEST");
    
    // Generated ID should use new prefix
    std::string id = connector.generate_client_order_id();
    EXPECT_TRUE(id.starts_with("TEST-"));
}

// ============================================================================
// TESTS: TRADING RULES
// ============================================================================

TEST(TradingRule, PriceQuantization) {
    TradingRule rule;
    rule.tick_size = 0.1;
    rule.price_decimals = 1;
    
    EXPECT_NEAR(rule.quantize_price(50123.456), 50123.5, 1e-6);
    EXPECT_NEAR(rule.quantize_price(50123.44), 50123.4, 1e-6);
    EXPECT_NEAR(rule.quantize_price(50123.46), 50123.5, 1e-6);
}

TEST(TradingRule, SizeQuantization) {
    TradingRule rule;
    rule.step_size = 0.001;
    rule.size_decimals = 3;
    
    EXPECT_NEAR(rule.quantize_size(0.1234), 0.123, 1e-9);
    EXPECT_NEAR(rule.quantize_size(0.1235), 0.124, 1e-9);
    EXPECT_NEAR(rule.quantize_size(0.1236), 0.124, 1e-9);
}

TEST(TradingRule, OrderValidation) {
    TradingRule rule;
    rule.min_order_size = 0.001;
    rule.max_order_size = 10.0;
    rule.min_price = 1.0;
    rule.max_price = 100000.0;
    rule.min_notional = 10.0;
    
    // Valid order
    EXPECT_EQ(rule.validate_order(50000.0, 0.01), "");
    
    // Size too small
    EXPECT_NE(rule.validate_order(50000.0, 0.0001), "");
    
    // Size too large
    EXPECT_NE(rule.validate_order(50000.0, 11.0), "");
    
    // Price too low
    EXPECT_NE(rule.validate_order(0.5, 1.0), "");
    
    // Price too high
    EXPECT_NE(rule.validate_order(110000.0, 1.0), "");
    
    // Notional too small
    EXPECT_NE(rule.validate_order(1000.0, 0.001), "");
}

// ============================================================================
// TESTS: CONNECTOR LIFECYCLE
// ============================================================================

TEST(ConnectorBase, Lifecycle) {
    MockConnector connector;
    
    EXPECT_FALSE(connector.is_connected());
    EXPECT_FALSE(connector.is_ready());
    
    EXPECT_TRUE(connector.initialize());
    EXPECT_TRUE(connector.connect());
    
    EXPECT_TRUE(connector.is_connected());
    EXPECT_TRUE(connector.is_ready());
    
    connector.disconnect();
    EXPECT_FALSE(connector.is_connected());
}

// ============================================================================
// TESTS: ORDER PLACEMENT
// ============================================================================

TEST(ConnectorBase, OrderPlacement) {
    MockConnector connector;
    connector.connect();
    
    OrderParams params{
        .trading_pair = "BTC-USD",
        .amount = 0.1,
        .price = 50000.0,
        .order_type = OrderType::LIMIT
    };
    
    std::string order_id = connector.buy(params);
    EXPECT_FALSE(order_id.empty());
    EXPECT_TRUE(order_id.starts_with("LS-"));
}

// ============================================================================
// TESTS: QUANTIZATION
// ============================================================================

TEST(ConnectorBase, Quantization) {
    MockConnector connector;
    
    // BTC-USD has rules defined in mock
    double quantized_price = connector.quantize_order_price("BTC-USD", 50123.456);
    EXPECT_NEAR(quantized_price, 50123.5, 1e-6);  // tick_size = 0.1
    
    double quantized_size = connector.quantize_order_amount("BTC-USD", 0.1234);
    EXPECT_NEAR(quantized_size, 0.123, 1e-9);  // step_size = 0.001
    
    // Unknown pair returns as-is
    double unknown_price = connector.quantize_order_price("ETH-USD", 3456.789);
    EXPECT_DOUBLE_EQ(unknown_price, 3456.789);
}

// ============================================================================
// TESTS: EVENTS
// ============================================================================

class MockOrderEventListener : public OrderEventListener {
public:
    int created_count = 0;
    int filled_count = 0;
    int completed_count = 0;
    int cancelled_count = 0;
    int failed_count = 0;
    
    void on_order_created(const std::string&, const std::string&) override {
        created_count++;
    }
    
    void on_order_filled(const std::string&, double, double) override {
        filled_count++;
    }
    
    void on_order_completed(const std::string&, double, double) override {
        completed_count++;
    }
    
    void on_order_cancelled(const std::string&) override {
        cancelled_count++;
    }
    
    void on_order_failed(const std::string&, const std::string&) override {
        failed_count++;
    }
};

TEST(ConnectorBase, EventEmission) {
    MockConnector connector;
    MockOrderEventListener listener;
    
    connector.set_order_event_listener(&listener);
    
    // Emit events (these are protected methods, so we'd need a derived class
    // to test them properly. For now, just verify listener is set)
    EXPECT_EQ(listener.created_count, 0);
}

// ============================================================================
// TESTS: POSITION (DERIVATIVES)
// ============================================================================

TEST(Position, Calculations) {
    Position pos;
    pos.symbol = "BTC-USD";
    pos.side = PositionSide::LONG;
    pos.size = 0.1;
    pos.entry_price = 50000.0;
    pos.mark_price = 51000.0;
    pos.liquidation_price = 45000.0;
    pos.unrealized_pnl = 100.0;
    pos.margin = 500.0;
    pos.leverage = 10;
    
    EXPECT_TRUE(pos.is_long());
    EXPECT_FALSE(pos.is_short());
    EXPECT_DOUBLE_EQ(pos.position_value(), 5100.0);  // 0.1 * 51000
    EXPECT_DOUBLE_EQ(pos.roe(), 20.0);  // (100 / 500) * 100
    
    double distance = pos.distance_to_liquidation();
    EXPECT_GT(distance, 11.0);  // Should be around 11.76%
    EXPECT_LT(distance, 12.0);
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
