/**
 * @file test_order_tracking.cpp
 * @brief Unit tests for order tracking (Phase 2)
 */

#include <gtest/gtest.h>
#include "connector/in_flight_order.h"
#include "connector/client_order_tracker.h"
#include <thread>
#include <chrono>

using namespace latentspeed::connector;
using namespace std::chrono_literals;

// ============================================================================
// TESTS: ORDER STATE ENUM
// ============================================================================

TEST(OrderState, EnumToString) {
    EXPECT_EQ(to_string(OrderState::PENDING_CREATE), "PENDING_CREATE");
    EXPECT_EQ(to_string(OrderState::PENDING_SUBMIT), "PENDING_SUBMIT");
    EXPECT_EQ(to_string(OrderState::OPEN), "OPEN");
    EXPECT_EQ(to_string(OrderState::PARTIALLY_FILLED), "PARTIALLY_FILLED");
    EXPECT_EQ(to_string(OrderState::FILLED), "FILLED");
    EXPECT_EQ(to_string(OrderState::PENDING_CANCEL), "PENDING_CANCEL");
    EXPECT_EQ(to_string(OrderState::CANCELLED), "CANCELLED");
    EXPECT_EQ(to_string(OrderState::FAILED), "FAILED");
    EXPECT_EQ(to_string(OrderState::EXPIRED), "EXPIRED");
}

// ============================================================================
// TESTS: IN-FLIGHT ORDER
// ============================================================================

TEST(InFlightOrder, DefaultState) {
    InFlightOrder order;
    order.client_order_id = "test_order_1";
    order.trading_pair = "BTC-USD";
    order.amount = 0.1;
    order.price = 50000.0;
    
    EXPECT_EQ(order.current_state, OrderState::PENDING_CREATE);
    EXPECT_EQ(order.filled_amount, 0.0);
    EXPECT_EQ(order.average_fill_price, 0.0);
    EXPECT_TRUE(order.trade_fills.empty());
    EXPECT_FALSE(order.exchange_order_id.has_value());
}

TEST(InFlightOrder, StateQueries) {
    InFlightOrder order;
    order.amount = 1.0;
    order.filled_amount = 0.5;
    
    // PENDING_CREATE state
    order.current_state = OrderState::PENDING_CREATE;
    EXPECT_TRUE(order.is_active());
    EXPECT_FALSE(order.is_done());
    EXPECT_FALSE(order.is_fillable());
    
    // OPEN state
    order.current_state = OrderState::OPEN;
    EXPECT_TRUE(order.is_active());
    EXPECT_FALSE(order.is_done());
    EXPECT_TRUE(order.is_fillable());
    
    // PARTIALLY_FILLED state
    order.current_state = OrderState::PARTIALLY_FILLED;
    EXPECT_TRUE(order.is_active());
    EXPECT_FALSE(order.is_done());
    EXPECT_TRUE(order.is_fillable());
    
    // FILLED state
    order.current_state = OrderState::FILLED;
    EXPECT_FALSE(order.is_active());
    EXPECT_TRUE(order.is_done());
    EXPECT_FALSE(order.is_fillable());
    
    // CANCELLED state
    order.current_state = OrderState::CANCELLED;
    EXPECT_FALSE(order.is_active());
    EXPECT_TRUE(order.is_done());
    EXPECT_FALSE(order.is_fillable());
    
    // FAILED state
    order.current_state = OrderState::FAILED;
    EXPECT_FALSE(order.is_active());
    EXPECT_TRUE(order.is_done());
    EXPECT_FALSE(order.is_fillable());
}

TEST(InFlightOrder, RemainingAmount) {
    InFlightOrder order;
    order.amount = 1.0;
    order.filled_amount = 0.0;
    
    EXPECT_DOUBLE_EQ(order.remaining_amount(), 1.0);
    
    order.filled_amount = 0.3;
    EXPECT_DOUBLE_EQ(order.remaining_amount(), 0.7);
    
    order.filled_amount = 1.0;
    EXPECT_DOUBLE_EQ(order.remaining_amount(), 0.0);
}

TEST(InFlightOrder, AsyncExchangeOrderId) {
    InFlightOrder order;
    order.client_order_id = "test_order_1";
    
    // Start async wait in separate thread
    std::thread waiter([&order]() {
        auto result = order.get_exchange_order_id_async(2s);
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(*result, "exchange_123");
    });
    
    // Simulate delay before setting exchange order ID
    std::this_thread::sleep_for(100ms);
    order.exchange_order_id = "exchange_123";
    order.notify_exchange_order_id_ready();
    
    waiter.join();
}

TEST(InFlightOrder, AsyncExchangeOrderIdTimeout) {
    InFlightOrder order;
    order.client_order_id = "test_order_1";
    
    // This should timeout
    auto result = order.get_exchange_order_id_async(100ms);
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// TESTS: CLIENT ORDER TRACKER
// ============================================================================

TEST(ClientOrderTracker, StartStopTracking) {
    ClientOrderTracker tracker;
    
    InFlightOrder order;
    order.client_order_id = "test_order_1";
    order.trading_pair = "BTC-USD";
    order.amount = 0.1;
    order.price = 50000.0;
    
    EXPECT_EQ(tracker.active_order_count(), 0);
    
    tracker.start_tracking(order);
    EXPECT_EQ(tracker.active_order_count(), 1);
    
    tracker.stop_tracking("test_order_1");
    EXPECT_EQ(tracker.active_order_count(), 0);
}

TEST(ClientOrderTracker, GetOrder) {
    ClientOrderTracker tracker;
    
    InFlightOrder order;
    order.client_order_id = "test_order_1";
    order.trading_pair = "BTC-USD";
    order.amount = 0.1;
    
    tracker.start_tracking(order);
    
    // Get by client order ID
    auto result = tracker.get_order("test_order_1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->client_order_id, "test_order_1");
    EXPECT_EQ(result->trading_pair, "BTC-USD");
    
    // Non-existent order
    auto missing = tracker.get_order("non_existent");
    EXPECT_FALSE(missing.has_value());
}

TEST(ClientOrderTracker, GetOrderByExchangeId) {
    ClientOrderTracker tracker;
    
    InFlightOrder order;
    order.client_order_id = "test_order_1";
    order.exchange_order_id = "exchange_123";
    order.trading_pair = "BTC-USD";
    
    tracker.start_tracking(order);
    
    // Get by exchange order ID
    auto result = tracker.get_order_by_exchange_id("exchange_123");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->client_order_id, "test_order_1");
    
    // Non-existent exchange order ID
    auto missing = tracker.get_order_by_exchange_id("non_existent");
    EXPECT_FALSE(missing.has_value());
}

TEST(ClientOrderTracker, OrderLifecycle) {
    ClientOrderTracker tracker;
    tracker.set_auto_cleanup(false);  // Disable auto-cleanup for this test
    
    // Create order
    InFlightOrder order;
    order.client_order_id = "test_order_1";
    order.trading_pair = "BTC-USD";
    order.order_type = OrderType::LIMIT;
    order.trade_type = TradeType::BUY;
    order.price = 50000.0;
    order.amount = 0.1;
    order.creation_timestamp = 1234567890;
    
    // Start tracking
    tracker.start_tracking(order);
    EXPECT_EQ(tracker.active_order_count(), 1);
    
    // Process order created update
    OrderUpdate created_update{
        .client_order_id = "test_order_1",
        .exchange_order_id = "exchange_123",
        .trading_pair = "BTC-USD",
        .new_state = OrderState::OPEN,
        .update_timestamp = 1234567891
    };
    tracker.process_order_update(created_update);
    
    auto tracked = tracker.get_order("test_order_1");
    ASSERT_TRUE(tracked.has_value());
    EXPECT_EQ(tracked->current_state, OrderState::OPEN);
    EXPECT_EQ(tracked->exchange_order_id, "exchange_123");
    
    // Process partial fill
    TradeUpdate fill1{
        .trade_id = "trade_1",
        .client_order_id = "test_order_1",
        .exchange_order_id = "exchange_123",
        .trading_pair = "BTC-USD",
        .fill_price = 50100.0,
        .fill_base_amount = 0.05,
        .fill_quote_amount = 2505.0,
        .fee_currency = "USDT",
        .fee_amount = 2.505,
        .fill_timestamp = 1234567892
    };
    tracker.process_trade_update(fill1);
    
    tracked = tracker.get_order("test_order_1");
    ASSERT_TRUE(tracked.has_value());
    EXPECT_EQ(tracked->current_state, OrderState::PARTIALLY_FILLED);
    EXPECT_NEAR(tracked->filled_amount, 0.05, 1e-9);
    EXPECT_EQ(tracked->trade_fills.size(), 1);
    
    // Process second fill (completes order)
    TradeUpdate fill2{
        .trade_id = "trade_2",
        .client_order_id = "test_order_1",
        .exchange_order_id = "exchange_123",
        .trading_pair = "BTC-USD",
        .fill_price = 50200.0,
        .fill_base_amount = 0.05,
        .fill_quote_amount = 2510.0,
        .fee_currency = "USDT",
        .fee_amount = 2.51,
        .fill_timestamp = 1234567893
    };
    tracker.process_trade_update(fill2);
    
    tracked = tracker.get_order("test_order_1");
    ASSERT_TRUE(tracked.has_value());
    EXPECT_EQ(tracked->current_state, OrderState::FILLED);
    EXPECT_NEAR(tracked->filled_amount, 0.1, 1e-9);
    EXPECT_EQ(tracked->trade_fills.size(), 2);
    
    // Average fill price should be (2505 + 2510) / 0.1 = 50150
    EXPECT_NEAR(tracked->average_fill_price, 50150.0, 1e-6);
}

TEST(ClientOrderTracker, FillableOrders) {
    ClientOrderTracker tracker;
    tracker.set_auto_cleanup(false);
    
    // Create multiple orders in different states
    InFlightOrder order1;
    order1.client_order_id = "order_1";
    order1.current_state = OrderState::OPEN;
    tracker.start_tracking(order1);
    
    InFlightOrder order2;
    order2.client_order_id = "order_2";
    order2.current_state = OrderState::PARTIALLY_FILLED;
    tracker.start_tracking(order2);
    
    InFlightOrder order3;
    order3.client_order_id = "order_3";
    order3.current_state = OrderState::FILLED;
    tracker.start_tracking(order3);
    
    InFlightOrder order4;
    order4.client_order_id = "order_4";
    order4.current_state = OrderState::PENDING_SUBMIT;
    tracker.start_tracking(order4);
    
    // Get fillable orders
    auto fillable = tracker.all_fillable_orders();
    EXPECT_EQ(fillable.size(), 2);  // Only OPEN and PARTIALLY_FILLED
    EXPECT_TRUE(fillable.count("order_1") > 0);
    EXPECT_TRUE(fillable.count("order_2") > 0);
    EXPECT_FALSE(fillable.count("order_3") > 0);
    EXPECT_FALSE(fillable.count("order_4") > 0);
}

TEST(ClientOrderTracker, AutoCleanup) {
    ClientOrderTracker tracker;
    tracker.set_auto_cleanup(true);  // Enable auto-cleanup
    
    InFlightOrder order;
    order.client_order_id = "test_order_1";
    order.trading_pair = "BTC-USD";
    order.amount = 0.1;
    
    tracker.start_tracking(order);
    EXPECT_EQ(tracker.active_order_count(), 1);
    
    // Mark as filled (terminal state)
    OrderUpdate update{
        .client_order_id = "test_order_1",
        .trading_pair = "BTC-USD",
        .new_state = OrderState::FILLED,
        .update_timestamp = 1234567890
    };
    tracker.process_order_update(update);
    
    // Should be auto-removed
    EXPECT_EQ(tracker.active_order_count(), 0);
}

TEST(ClientOrderTracker, ConcurrentAccess) {
    ClientOrderTracker tracker;
    
    // Concurrent writes
    std::vector<std::thread> writers;
    for (int i = 0; i < 10; ++i) {
        writers.emplace_back([&tracker, i]() {
            for (int j = 0; j < 100; ++j) {
                InFlightOrder order;
                order.client_order_id = "order_" + std::to_string(i * 100 + j);
                order.trading_pair = "BTC-USD";
                order.amount = 0.1;
                tracker.start_tracking(order);
            }
        });
    }
    
    for (auto& t : writers) t.join();
    
    EXPECT_EQ(tracker.active_order_count(), 1000);
    
    // Concurrent reads
    std::vector<std::thread> readers;
    for (int i = 0; i < 10; ++i) {
        readers.emplace_back([&tracker, i]() {
            for (int j = 0; j < 100; ++j) {
                auto order = tracker.get_order("order_" + std::to_string(i * 100 + j));
                EXPECT_TRUE(order.has_value());
            }
        });
    }
    
    for (auto& t : readers) t.join();
}

TEST(ClientOrderTracker, EventCallback) {
    ClientOrderTracker tracker;
    
    int event_count = 0;
    OrderEventType last_event_type;
    std::string last_order_id;
    
    tracker.set_event_callback([&](OrderEventType type, const std::string& order_id) {
        event_count++;
        last_event_type = type;
        last_order_id = order_id;
    });
    
    InFlightOrder order;
    order.client_order_id = "test_order_1";
    tracker.start_tracking(order);
    
    // Trigger update event
    OrderUpdate update{
        .client_order_id = "test_order_1",
        .new_state = OrderState::OPEN,
        .update_timestamp = 1234567890
    };
    tracker.process_order_update(update);
    
    EXPECT_EQ(event_count, 1);
    EXPECT_EQ(last_event_type, OrderEventType::ORDER_UPDATE);
    EXPECT_EQ(last_order_id, "test_order_1");
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
