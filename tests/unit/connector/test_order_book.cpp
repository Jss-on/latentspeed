/**
 * @file test_order_book.cpp
 * @brief Unit tests for OrderBook and Data Sources (Phase 3)
 */

#include <gtest/gtest.h>
#include "connector/order_book.h"
#include "connector/order_book_tracker_data_source.h"
#include "connector/user_stream_tracker_data_source.h"

using namespace latentspeed::connector;

// ============================================================================
// TESTS: ORDER BOOK
// ============================================================================

TEST(OrderBook, DefaultState) {
    OrderBook ob;
    ob.trading_pair = "BTC-USD";
    
    EXPECT_TRUE(ob.bids.empty());
    EXPECT_TRUE(ob.asks.empty());
    EXPECT_FALSE(ob.is_valid());
    EXPECT_FALSE(ob.best_bid().has_value());
    EXPECT_FALSE(ob.best_ask().has_value());
}

TEST(OrderBook, ApplySnapshot) {
    OrderBook ob;
    ob.trading_pair = "BTC-USD";
    
    std::map<double, double> bids = {
        {50000.0, 1.5},
        {49999.0, 2.0},
        {49998.0, 0.5}
    };
    
    std::map<double, double> asks = {
        {50001.0, 1.0},
        {50002.0, 1.5},
        {50003.0, 2.0}
    };
    
    ob.apply_snapshot(bids, asks, 12345);
    
    EXPECT_TRUE(ob.is_valid());
    EXPECT_EQ(ob.sequence, 12345);
    EXPECT_EQ(ob.bids.size(), 3);
    EXPECT_EQ(ob.asks.size(), 3);
    
    // Check best prices
    ASSERT_TRUE(ob.best_bid().has_value());
    ASSERT_TRUE(ob.best_ask().has_value());
    EXPECT_DOUBLE_EQ(*ob.best_bid(), 50000.0);
    EXPECT_DOUBLE_EQ(*ob.best_ask(), 50001.0);
    
    // Check best sizes
    EXPECT_DOUBLE_EQ(*ob.best_bid_size(), 1.5);
    EXPECT_DOUBLE_EQ(*ob.best_ask_size(), 1.0);
}

TEST(OrderBook, ApplyDelta) {
    OrderBook ob;
    ob.trading_pair = "BTC-USD";
    
    // Initial state
    ob.apply_delta(50000.0, 1.0, true);   // Bid
    ob.apply_delta(50001.0, 1.0, false);  // Ask
    
    EXPECT_TRUE(ob.is_valid());
    EXPECT_DOUBLE_EQ(*ob.best_bid(), 50000.0);
    EXPECT_DOUBLE_EQ(*ob.best_ask(), 50001.0);
    
    // Update bid
    ob.apply_delta(50000.0, 2.0, true);   // Increase size
    EXPECT_DOUBLE_EQ(*ob.best_bid_size(), 2.0);
    
    // Remove bid
    ob.apply_delta(50000.0, 0.0, true);   // Size = 0 removes level
    EXPECT_FALSE(ob.best_bid().has_value());
}

TEST(OrderBook, MidPriceAndSpread) {
    OrderBook ob;
    ob.trading_pair = "BTC-USD";
    
    ob.apply_delta(50000.0, 1.0, true);   // Bid
    ob.apply_delta(50010.0, 1.0, false);  // Ask
    
    // Mid price
    ASSERT_TRUE(ob.mid_price().has_value());
    EXPECT_DOUBLE_EQ(*ob.mid_price(), 50005.0);
    
    // Spread
    ASSERT_TRUE(ob.spread().has_value());
    EXPECT_DOUBLE_EQ(*ob.spread(), 10.0);
    
    // Spread in basis points
    ASSERT_TRUE(ob.spread_bps().has_value());
    EXPECT_NEAR(*ob.spread_bps(), 2.0, 0.01);  // (10/50000) * 10000 = 2 bps
}

TEST(OrderBook, GetTopLevels) {
    OrderBook ob;
    ob.trading_pair = "BTC-USD";
    
    // Add 5 bid levels
    for (int i = 0; i < 5; ++i) {
        ob.apply_delta(50000.0 - i, 1.0 + i * 0.1, true);
    }
    
    // Add 5 ask levels
    for (int i = 0; i < 5; ++i) {
        ob.apply_delta(50001.0 + i, 1.0 + i * 0.1, false);
    }
    
    // Get top 3 bids
    auto top_bids = ob.get_top_bids(3);
    EXPECT_EQ(top_bids.size(), 3);
    EXPECT_DOUBLE_EQ(top_bids[0].price, 50000.0);  // Best bid
    EXPECT_DOUBLE_EQ(top_bids[1].price, 49999.0);
    EXPECT_DOUBLE_EQ(top_bids[2].price, 49998.0);
    
    // Get top 3 asks
    auto top_asks = ob.get_top_asks(3);
    EXPECT_EQ(top_asks.size(), 3);
    EXPECT_DOUBLE_EQ(top_asks[0].price, 50001.0);  // Best ask
    EXPECT_DOUBLE_EQ(top_asks[1].price, 50002.0);
    EXPECT_DOUBLE_EQ(top_asks[2].price, 50003.0);
}

TEST(OrderBook, Clear) {
    OrderBook ob;
    ob.trading_pair = "BTC-USD";
    
    ob.apply_delta(50000.0, 1.0, true);
    ob.apply_delta(50001.0, 1.0, false);
    EXPECT_TRUE(ob.is_valid());
    
    ob.clear();
    EXPECT_FALSE(ob.is_valid());
    EXPECT_EQ(ob.sequence, 0);
}

// ============================================================================
// TESTS: ORDERBOOK MESSAGE
// ============================================================================

TEST(OrderBookMessage, Types) {
    OrderBookMessage msg;
    
    msg.type = OrderBookMessage::Type::SNAPSHOT;
    msg.trading_pair = "BTC-USD";
    msg.timestamp = 1234567890;
    msg.data = nlohmann::json{{"price", 50000.0}};
    
    EXPECT_EQ(msg.type, OrderBookMessage::Type::SNAPSHOT);
    EXPECT_EQ(msg.trading_pair, "BTC-USD");
    EXPECT_EQ(msg.data["price"], 50000.0);
}

// ============================================================================
// TESTS: USER STREAM MESSAGE
// ============================================================================

TEST(UserStreamMessage, Types) {
    UserStreamMessage msg;
    
    msg.type = UserStreamMessage::Type::ORDER_UPDATE;
    msg.timestamp = 1234567890;
    msg.data = nlohmann::json{{"order_id", "12345"}};
    
    EXPECT_EQ(msg.type, UserStreamMessage::Type::ORDER_UPDATE);
    EXPECT_EQ(msg.data["order_id"], "12345");
}

// ============================================================================
// TESTS: MOCK DATA SOURCES
// ============================================================================

class MockOrderBookDataSource : public OrderBookTrackerDataSource {
public:
    bool initialize() override { return true; }
    void start() override { started = true; }
    void stop() override { started = false; }
    bool is_connected() const override { return started; }
    
    std::optional<OrderBook> get_snapshot(const std::string& trading_pair) override {
        OrderBook ob;
        ob.trading_pair = trading_pair;
        ob.apply_delta(50000.0, 1.0, true);
        ob.apply_delta(50001.0, 1.0, false);
        return ob;
    }
    
    void subscribe_orderbook(const std::string& trading_pair) override {
        subscribed_pairs.push_back(trading_pair);
    }
    
    void unsubscribe_orderbook(const std::string& trading_pair) override {
        // Remove from vector
    }
    
    // Test helper
    void simulate_message(const OrderBookMessage& msg) {
        emit_message(msg);
    }
    
    bool started = false;
    std::vector<std::string> subscribed_pairs;
};

class MockUserStreamDataSource : public UserStreamTrackerDataSource {
public:
    bool initialize() override { return true; }
    void start() override { started = true; }
    void stop() override { started = false; }
    bool is_connected() const override { return started; }
    
    void subscribe_to_order_updates() override {
        subscribed_orders = true;
    }
    
    // Test helper
    void simulate_message(const UserStreamMessage& msg) {
        emit_message(msg);
    }
    
    bool started = false;
    bool subscribed_orders = false;
};

TEST(OrderBookDataSource, Lifecycle) {
    MockOrderBookDataSource source;
    
    EXPECT_FALSE(source.is_connected());
    
    EXPECT_TRUE(source.initialize());
    source.start();
    
    EXPECT_TRUE(source.is_connected());
    
    source.stop();
    EXPECT_FALSE(source.is_connected());
}

TEST(OrderBookDataSource, Subscription) {
    MockOrderBookDataSource source;
    
    source.subscribe_orderbook("BTC-USD");
    source.subscribe_orderbook("ETH-USD");
    
    EXPECT_EQ(source.subscribed_pairs.size(), 2);
    EXPECT_EQ(source.subscribed_pairs[0], "BTC-USD");
    EXPECT_EQ(source.subscribed_pairs[1], "ETH-USD");
}

TEST(OrderBookDataSource, MessageCallback) {
    MockOrderBookDataSource source;
    
    int message_count = 0;
    OrderBookMessage received_msg;
    
    source.set_message_callback([&](const OrderBookMessage& msg) {
        message_count++;
        received_msg = msg;
    });
    
    // Simulate message
    OrderBookMessage msg{
        .type = OrderBookMessage::Type::SNAPSHOT,
        .trading_pair = "BTC-USD",
        .timestamp = 1234567890
    };
    
    source.simulate_message(msg);
    
    EXPECT_EQ(message_count, 1);
    EXPECT_EQ(received_msg.type, OrderBookMessage::Type::SNAPSHOT);
    EXPECT_EQ(received_msg.trading_pair, "BTC-USD");
}

TEST(UserStreamDataSource, Lifecycle) {
    MockUserStreamDataSource source;
    
    EXPECT_FALSE(source.is_connected());
    
    EXPECT_TRUE(source.initialize());
    source.start();
    
    EXPECT_TRUE(source.is_connected());
    
    source.stop();
    EXPECT_FALSE(source.is_connected());
}

TEST(UserStreamDataSource, Subscription) {
    MockUserStreamDataSource source;
    
    source.subscribe_to_order_updates();
    EXPECT_TRUE(source.subscribed_orders);
}

TEST(UserStreamDataSource, MessageCallback) {
    MockUserStreamDataSource source;
    
    int message_count = 0;
    UserStreamMessage received_msg;
    
    source.set_message_callback([&](const UserStreamMessage& msg) {
        message_count++;
        received_msg = msg;
    });
    
    // Simulate message
    UserStreamMessage msg{
        .type = UserStreamMessage::Type::ORDER_UPDATE,
        .timestamp = 1234567890
    };
    msg.data = nlohmann::json{{"order_id", "test_1"}};
    
    source.simulate_message(msg);
    
    EXPECT_EQ(message_count, 1);
    EXPECT_EQ(received_msg.type, UserStreamMessage::Type::ORDER_UPDATE);
    EXPECT_EQ(received_msg.data["order_id"], "test_1");
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
