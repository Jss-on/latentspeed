#include "connector/hyperliquid_integrated_connector.h"
#include "connector/hyperliquid_auth.h"
#include "exchange_interface.h"
#include <zmq.hpp>
#include <iostream>
#include <thread>
#include <chrono>

using namespace latentspeed;

/**
 * @brief Example showing how to use the integrated Hyperliquid connector
 * 
 * This demonstrates:
 * 1. Reusing your existing marketstream (HyperliquidExchange)
 * 2. Phase 5 user stream for authenticated order updates
 * 3. ZMQ publishing for order events
 * 4. Non-blocking order placement
 */

// ZMQ subscriber example (separate thread/process)
void zmq_order_subscriber_example(zmq::context_t& context) {
    zmq::socket_t subscriber(context, zmq::socket_type::sub);
    subscriber.connect("tcp://localhost:5556");
    
    // Subscribe to all order events
    subscriber.set(zmq::sockopt::subscribe, "orders.hyperliquid");
    
    std::cout << "ZMQ subscriber listening for order events...\n";
    
    while (true) {
        // Receive topic
        zmq::message_t topic_msg;
        auto result = subscriber.recv(topic_msg, zmq::recv_flags::none);
        if (!result) break;
        
        std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
        
        // Receive message body
        zmq::message_t body_msg;
        result = subscriber.recv(body_msg, zmq::recv_flags::none);
        if (!result) break;
        
        std::string body(static_cast<char*>(body_msg.data()), body_msg.size());
        
        std::cout << "[ZMQ] Topic: " << topic << "\n";
        std::cout << "[ZMQ] Body: " << body << "\n\n";
    }
}

int main() {
    try {
        std::cout << "=== Hyperliquid Integrated Connector Example ===\n\n";
        
        // 1. Create ZMQ context (reuse your existing one!)
        auto zmq_context = std::make_shared<zmq::context_t>(1);
        
        // 2. Create or reuse your existing marketstream exchange
        auto existing_exchange = std::make_shared<HyperliquidExchange>();
        
        // Initialize your marketstream (you probably already do this)
        existing_exchange->initialize();
        existing_exchange->start();
        
        std::cout << "✓ Your existing marketstream is running\n";
        
        // 3. Create Hyperliquid auth
        std::string private_key = "0x...";  // Your private key
        auto auth = std::make_shared<HyperliquidAuth>(private_key);
        
        std::cout << "✓ Authentication configured\n";
        std::cout << "  Address: " << auth->get_address() << "\n\n";
        
        // 4. Create integrated connector
        bool testnet = true;
        std::string zmq_endpoint = "tcp://*:5556";
        
        HyperliquidIntegratedConnector connector(
            auth,
            existing_exchange,     // Reuse your existing exchange!
            zmq_context,           // Reuse your ZMQ context!
            zmq_endpoint,
            testnet
        );
        
        std::cout << "✓ Integrated connector created\n";
        std::cout << "  Using existing marketstream: YES\n";
        std::cout << "  ZMQ endpoint: " << zmq_endpoint << "\n\n";
        
        // 5. Initialize and start
        if (!connector.initialize()) {
            std::cerr << "Failed to initialize connector\n";
            return 1;
        }
        std::cout << "✓ Connector initialized\n";
        
        connector.start();
        std::cout << "✓ Connector started\n";
        std::cout << "  Market data: Your existing marketstream\n";
        std::cout << "  User stream: Authenticated WebSocket\n";
        std::cout << "  Order events: Publishing to ZMQ\n\n";
        
        // 6. Start ZMQ subscriber in separate thread (simulate your other system components)
        std::thread subscriber_thread([&zmq_context]() {
            zmq_order_subscriber_example(*zmq_context);
        });
        subscriber_thread.detach();
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 7. Place orders (non-blocking!)
        std::cout << "=== Placing Orders ===\n\n";
        
        // Example 1: Buy limit order
        {
            OrderParams params;
            params.trading_pair = "BTC-USD";
            params.amount = 0.001;
            params.price = 50000.0;
            params.order_type = OrderType::LIMIT;
            
            std::string order_id = connector.buy(params);
            std::cout << "✓ Buy order placed (non-blocking!)\n";
            std::cout << "  Order ID: " << order_id << "\n";
            std::cout << "  Returned in: <1ms\n";
            std::cout << "  → Order tracked BEFORE exchange submission\n";
            std::cout << "  → ZMQ event will be published automatically\n\n";
        }
        
        // Example 2: Sell market order
        {
            OrderParams params;
            params.trading_pair = "ETH-USD";
            params.amount = 0.1;
            params.order_type = OrderType::MARKET;
            
            std::string order_id = connector.sell(params);
            std::cout << "✓ Sell market order placed\n";
            std::cout << "  Order ID: " << order_id << "\n\n";
        }
        
        // Example 3: Post-only limit maker
        {
            OrderParams params;
            params.trading_pair = "SOL-USD";
            params.amount = 10.0;
            params.price = 100.0;
            params.order_type = OrderType::LIMIT_MAKER;
            
            std::string order_id = connector.buy(params);
            std::cout << "✓ Post-only limit maker placed\n";
            std::cout << "  Order ID: " << order_id << "\n\n";
        }
        
        // 8. Query orders
        std::cout << "=== Querying Orders ===\n\n";
        
        auto open_orders = connector.get_open_orders("BTC-USD");
        std::cout << "Open orders for BTC-USD: " << open_orders.size() << "\n";
        
        for (const auto& order : open_orders) {
            std::cout << "  - " << order.client_order_id 
                     << " [" << order_state_to_string(order.current_state) << "]\n";
            std::cout << "    Amount: " << order.amount 
                     << " @ " << order.price << "\n";
        }
        std::cout << "\n";
        
        // 9. Cancel order
        if (!open_orders.empty()) {
            std::cout << "=== Cancelling Order ===\n\n";
            
            const auto& order_to_cancel = open_orders[0];
            auto future = connector.cancel(order_to_cancel.trading_pair, 
                                          order_to_cancel.client_order_id);
            
            std::cout << "Cancel request sent (async)\n";
            std::cout << "Waiting for result...\n";
            
            bool success = future.get();
            std::cout << (success ? "✓" : "✗") << " Cancellation " 
                     << (success ? "successful" : "failed") << "\n\n";
        }
        
        // 10. Access to components
        std::cout << "=== Component Access ===\n\n";
        
        // You can still access your original marketstream
        auto marketstream = connector.get_marketstream_exchange();
        std::cout << "✓ Can access original marketstream\n";
        std::cout << "  Connected: " << (marketstream->is_connected() ? "yes" : "no") << "\n";
        
        // You can access ZMQ publisher directly if needed
        auto zmq_pub = connector.get_zmq_publisher();
        std::cout << "✓ Can access ZMQ publisher\n";
        std::cout << "  Endpoint: " << zmq_pub->get_endpoint() << "\n";
        std::cout << "  Topic: " << zmq_pub->get_topic_prefix() << "\n\n";
        
        // 11. Monitor for a while
        std::cout << "=== Monitoring (10 seconds) ===\n\n";
        std::cout << "Watch for:\n";
        std::cout << "  - Order state transitions\n";
        std::cout << "  - ZMQ events being published\n";
        std::cout << "  - User stream updates\n";
        std::cout << "  - Market data from your existing stream\n\n";
        
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        // 12. Stop
        std::cout << "=== Stopping ===\n\n";
        connector.stop();
        std::cout << "✓ Connector stopped\n";
        std::cout << "  Your marketstream is still running (managed separately)\n\n";
        
        std::cout << "=== Summary ===\n\n";
        std::cout << "Integration achieved:\n";
        std::cout << "  ✓ Reused existing marketstream (no duplication)\n";
        std::cout << "  ✓ Added Phase 5 user stream (authenticated)\n";
        std::cout << "  ✓ Publishing order events to ZMQ\n";
        std::cout << "  ✓ Non-blocking order placement\n";
        std::cout << "  ✓ Track before submit pattern\n";
        std::cout << "  ✓ Event-driven architecture\n\n";
        
        std::cout << "Next steps:\n";
        std::cout << "  1. Subscribe to ZMQ topics in your other components\n";
        std::cout << "  2. Integrate with your strategy framework\n";
        std::cout << "  3. Add database persistence for order history\n";
        std::cout << "  4. Connect to your risk engine\n\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
