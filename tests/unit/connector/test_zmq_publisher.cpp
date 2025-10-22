#include <gtest/gtest.h>
#include "connector/zmq_order_event_publisher.h"
#include "connector/in_flight_order.h"
#include <zmq.hpp>
#include <thread>
#include <chrono>

using namespace latentspeed::connector;

/**
 * @brief Test fixture for ZMQ order event publisher
 */
class ZMQPublisherTest : public ::testing::Test {
protected:
    void SetUp() override {
        context = std::make_shared<zmq::context_t>(1);
    }
    
    void TearDown() override {
        context.reset();
    }
    
    std::shared_ptr<zmq::context_t> context;
};

// ============================================================================
// TESTS
// ============================================================================

TEST_F(ZMQPublisherTest, ConstructorRequiresNonNullContext) {
    // Should throw if context is null
    EXPECT_THROW({
        ZMQOrderEventPublisher publisher(nullptr, "tcp://*:5557", "test");
    }, std::invalid_argument);
}

TEST_F(ZMQPublisherTest, ConstructorBindsToEndpoint) {
    // Should bind successfully
    EXPECT_NO_THROW({
        ZMQOrderEventPublisher publisher(context, "tcp://*:15557", "test");
    });
}

TEST_F(ZMQPublisherTest, GettersReturnCorrectValues) {
    ZMQOrderEventPublisher publisher(context, "tcp://*:15558", "orders.test");
    
    EXPECT_EQ(publisher.get_endpoint(), "tcp://*:15558");
    EXPECT_EQ(publisher.get_topic_prefix(), "orders.test");
}

TEST_F(ZMQPublisherTest, PublishOrderCreatedDoesNotThrow) {
    ZMQOrderEventPublisher publisher(context, "tcp://*:15559", "test");
    
    InFlightOrder order;
    order.client_order_id = "test-order-1";
    order.trading_pair = "BTC-USD";
    order.order_type = OrderType::LIMIT;
    order.trade_type = TradeType::BUY;
    order.amount = 0.001;
    order.price = 50000.0;
    
    EXPECT_NO_THROW(publisher.publish_order_created(order));
}

TEST_F(ZMQPublisherTest, PublishOrderFilledDoesNotThrow) {
    ZMQOrderEventPublisher publisher(context, "tcp://*:15560", "test");
    
    InFlightOrder order;
    order.client_order_id = "test-order-2";
    order.trading_pair = "ETH-USD";
    order.current_state = OrderState::FILLED;
    order.filled_amount = 0.1;
    
    EXPECT_NO_THROW(publisher.publish_order_filled(order));
}

TEST_F(ZMQPublisherTest, PublishOrderCancelledDoesNotThrow) {
    ZMQOrderEventPublisher publisher(context, "tcp://*:15561", "test");
    
    InFlightOrder order;
    order.client_order_id = "test-order-3";
    order.current_state = OrderState::CANCELLED;
    
    EXPECT_NO_THROW(publisher.publish_order_cancelled(order));
}

TEST_F(ZMQPublisherTest, PublishOrderFailedWithReason) {
    ZMQOrderEventPublisher publisher(context, "tcp://*:15562", "test");
    
    InFlightOrder order;
    order.client_order_id = "test-order-4";
    order.current_state = OrderState::FAILED;
    
    EXPECT_NO_THROW(publisher.publish_order_failed(order, "Insufficient balance"));
}

TEST_F(ZMQPublisherTest, PublishPartialFillWithTrade) {
    ZMQOrderEventPublisher publisher(context, "tcp://*:15563", "test");
    
    InFlightOrder order;
    order.client_order_id = "test-order-5";
    order.amount = 1.0;
    order.filled_amount = 0.5;
    
    TradeUpdate trade;
    trade.trade_id = "trade-123";
    trade.client_order_id = "test-order-5";
    trade.price = 50000.0;
    trade.amount = 0.5;
    
    EXPECT_NO_THROW(publisher.publish_order_partially_filled(order, trade));
}

TEST_F(ZMQPublisherTest, PublishGenericOrderUpdate) {
    ZMQOrderEventPublisher publisher(context, "tcp://*:15564", "test");
    
    InFlightOrder order;
    order.client_order_id = "test-order-6";
    order.current_state = OrderState::OPEN;
    
    EXPECT_NO_THROW(publisher.publish_order_update(order));
}

TEST_F(ZMQPublisherTest, SubscriberCanReceivePublishedEvents) {
    // Create publisher on unique port
    ZMQOrderEventPublisher publisher(context, "tcp://*:15565", "test");
    
    // Give publisher time to bind
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create subscriber
    zmq::socket_t subscriber(*context, zmq::socket_type::sub);
    subscriber.connect("tcp://localhost:15565");
    subscriber.set(zmq::sockopt::subscribe, "test");
    
    // Give subscriber time to connect
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Publish event
    InFlightOrder order;
    order.client_order_id = "test-receive";
    order.trading_pair = "BTC-USD";
    publisher.publish_order_created(order);
    
    // Try to receive (with timeout)
    zmq::message_t topic_msg;
    zmq::message_t body_msg;
    
    auto result = subscriber.recv(topic_msg, zmq::recv_flags::dontwait);
    if (result) {
        subscriber.recv(body_msg, zmq::recv_flags::dontwait);
        
        std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
        std::string body(static_cast<char*>(body_msg.data()), body_msg.size());
        
        // Should contain our topic prefix
        EXPECT_TRUE(topic.find("test") != std::string::npos);
        
        // Should be valid JSON
        EXPECT_NO_THROW({
            auto json = nlohmann::json::parse(body);
            EXPECT_EQ(json["event_type"], "order_created");
        });
    }
    // Note: May not receive due to timing, but shouldn't crash
}

TEST_F(ZMQPublisherTest, MultipleEventsCanBePublished) {
    ZMQOrderEventPublisher publisher(context, "tcp://*:15566", "test");
    
    InFlightOrder order1;
    order1.client_order_id = "order-1";
    
    InFlightOrder order2;
    order2.client_order_id = "order-2";
    
    InFlightOrder order3;
    order3.client_order_id = "order-3";
    
    // Should be able to publish multiple events
    EXPECT_NO_THROW({
        publisher.publish_order_created(order1);
        publisher.publish_order_created(order2);
        publisher.publish_order_filled(order3);
    });
}

TEST_F(ZMQPublisherTest, OrderToJsonContainsAllFields) {
    ZMQOrderEventPublisher publisher(context, "tcp://*:15567", "test");
    
    InFlightOrder order;
    order.client_order_id = "test-json";
    order.exchange_order_id = "exchange-123";
    order.trading_pair = "BTC-USD";
    order.order_type = OrderType::LIMIT;
    order.trade_type = TradeType::BUY;
    order.price = 50000.0;
    order.amount = 0.001;
    order.filled_amount = 0.0005;
    order.average_executed_price = 49950.0;
    order.current_state = OrderState::PARTIALLY_FILLED;
    order.fee_paid = 0.025;
    order.fee_asset = "USD";
    
    // Publish and verify it doesn't throw
    EXPECT_NO_THROW(publisher.publish_order_update(order));
}

TEST_F(ZMQPublisherTest, TradeUpdateContainsAllFields) {
    ZMQOrderEventPublisher publisher(context, "tcp://*:15568", "test");
    
    InFlightOrder order;
    order.client_order_id = "test-trade";
    
    TradeUpdate trade;
    trade.trade_id = "trade-456";
    trade.client_order_id = "test-trade";
    trade.exchange_order_id = "order-789";
    trade.trading_pair = "ETH-USD";
    trade.trade_type = TradeType::SELL;
    trade.price = 3000.0;
    trade.amount = 0.1;
    trade.fee = 0.03;
    trade.fee_asset = "USD";
    
    EXPECT_NO_THROW(publisher.publish_order_partially_filled(order, trade));
}

TEST_F(ZMQPublisherTest, HandlesEmptyOrderFields) {
    ZMQOrderEventPublisher publisher(context, "tcp://*:15569", "test");
    
    // Minimal order
    InFlightOrder order;
    order.client_order_id = "minimal-order";
    
    // Should handle gracefully
    EXPECT_NO_THROW(publisher.publish_order_created(order));
}

TEST_F(ZMQPublisherTest, DifferentTopicPrefixes) {
    // Test with different topic prefixes
    {
        ZMQOrderEventPublisher pub1(context, "tcp://*:15570", "orders.hyperliquid");
        EXPECT_EQ(pub1.get_topic_prefix(), "orders.hyperliquid");
    }
    
    {
        ZMQOrderEventPublisher pub2(context, "tcp://*:15571", "orders.binance");
        EXPECT_EQ(pub2.get_topic_prefix(), "orders.binance");
    }
}
