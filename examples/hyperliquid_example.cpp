/**
 * @file hyperliquid_example.cpp
 * @brief Example of Hyperliquid exchange market data streaming
 * @author jessiondiwangan@gmail.com
 * @date 2025
 * 
 * This example demonstrates how to:
 * 1. Create a Hyperliquid exchange instance
 * 2. Subscribe to market data (trades and orderbook)
 * 3. Parse incoming messages
 * 4. Handle real-time data streams
 */

#include "exchange_interface.h"
#include "market_data_provider.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace latentspeed;

/**
 * @brief Callback for handling market ticks
 */
void on_tick(const MarketTick& tick) {
    std::cout << "[TRADE] " 
              << tick.exchange << " " 
              << tick.symbol << " | "
              << "Price: " << tick.price << " | "
              << "Amount: " << tick.amount << " | "
              << "Side: " << tick.side << " | "
              << "ID: " << tick.trade_id 
              << std::endl;
}

/**
 * @brief Callback for handling order book snapshots
 */
void on_book(const OrderBookSnapshot& snapshot) {
    std::cout << "\n[ORDERBOOK] " 
              << snapshot.exchange << " " 
              << snapshot.symbol << std::endl;
    
    std::cout << "  Bids:" << std::endl;
    for (size_t i = 0; i < 5; ++i) {
        if (snapshot.bids[i].quantity > 0) {
            std::cout << "    " 
                      << snapshot.bids[i].price << " @ " 
                      << snapshot.bids[i].quantity 
                      << std::endl;
        }
    }
    
    std::cout << "  Asks:" << std::endl;
    for (size_t i = 0; i < 5; ++i) {
        if (snapshot.asks[i].quantity > 0) {
            std::cout << "    " 
                      << snapshot.asks[i].price << " @ " 
                      << snapshot.asks[i].quantity 
                      << std::endl;
        }
    }
    std::cout << std::endl;
}

/**
 * @brief Example 1: Basic Hyperliquid connection
 */
void example_basic_connection() {
    std::cout << "\n=== Example 1: Basic Hyperliquid Connection ===" << std::endl;
    
    // Create Hyperliquid exchange instance
    auto exchange = ExchangeFactory::create("hyperliquid");
    
    std::cout << "Exchange: " << exchange->get_name() << std::endl;
    std::cout << "WebSocket: wss://" 
              << exchange->get_websocket_host() 
              << ":" << exchange->get_websocket_port()
              << exchange->get_websocket_target() 
              << std::endl;
    
    // Test symbol normalization
    std::vector<std::string> test_symbols = {
        "BTC-USDT", "eth-usd", "SOLUSDT", "AVAX-PERP"
    };
    
    std::cout << "\nSymbol Normalization:" << std::endl;
    for (const auto& symbol : test_symbols) {
        std::cout << "  " << symbol 
                  << " -> " << exchange->normalize_symbol(symbol) 
                  << std::endl;
    }
}

/**
 * @brief Example 2: Generate subscription messages
 */
void example_subscription_generation() {
    std::cout << "\n=== Example 2: Subscription Generation ===" << std::endl;
    
    auto exchange = ExchangeFactory::create("hyperliquid");
    
    std::vector<std::string> symbols = {"BTC", "ETH", "SOL"};
    
    // Generate subscription for trades only
    std::string trades_sub = exchange->generate_subscription(symbols, true, false);
    std::cout << "\nTrades subscription:\n" << trades_sub << std::endl;
    
    // Generate subscription for orderbook only
    std::string book_sub = exchange->generate_subscription(symbols, false, true);
    std::cout << "\nOrderbook subscription:\n" << book_sub << std::endl;
    
    // Generate subscription for both
    std::string both_sub = exchange->generate_subscription(symbols, true, true);
    std::cout << "\nBoth subscription:\n" << both_sub << std::endl;
}

/**
 * @brief Example 3: Parse trade messages
 */
void example_parse_trade_message() {
    std::cout << "\n=== Example 3: Parse Trade Message ===" << std::endl;
    
    auto exchange = ExchangeFactory::create("hyperliquid");
    MarketTick tick;
    OrderBookSnapshot snapshot;
    
    // Example trade message from Hyperliquid
    std::string trade_message = R"({
        "channel": "trades",
        "data": [
            {
                "coin": "BTC",
                "side": "B",
                "px": "50000.0",
                "sz": "0.5",
                "hash": "0x123abc",
                "time": 1697234567890,
                "tid": 123456,
                "users": ["0xbuyer", "0xseller"]
            }
        ]
    })";
    
    auto msg_type = exchange->parse_message(trade_message, tick, snapshot);
    
    if (msg_type == ExchangeInterface::MessageType::TRADE) {
        std::cout << "\nParsed Trade:" << std::endl;
        std::cout << "  Exchange: " << tick.exchange << std::endl;
        std::cout << "  Symbol: " << tick.symbol << std::endl;
        std::cout << "  Price: " << tick.price << std::endl;
        std::cout << "  Amount: " << tick.amount << std::endl;
        std::cout << "  Side: " << tick.side << std::endl;
        std::cout << "  Trade ID: " << tick.trade_id << std::endl;
    } else {
        std::cout << "Failed to parse trade message" << std::endl;
    }
}

/**
 * @brief Example 4: Parse order book messages
 */
void example_parse_orderbook_message() {
    std::cout << "\n=== Example 4: Parse Order Book Message ===" << std::endl;
    
    auto exchange = ExchangeFactory::create("hyperliquid");
    MarketTick tick;
    OrderBookSnapshot snapshot;
    
    // Example order book message from Hyperliquid
    std::string book_message = R"({
        "channel": "l2Book",
        "data": {
            "coin": "ETH",
            "levels": [
                [
                    {"px": "3000.0", "sz": "10.5", "n": 3},
                    {"px": "2999.5", "sz": "5.2", "n": 2}
                ],
                [
                    {"px": "3000.5", "sz": "8.3", "n": 2},
                    {"px": "3001.0", "sz": "12.1", "n": 4}
                ]
            ],
            "time": 1697234567890
        }
    })";
    
    auto msg_type = exchange->parse_message(book_message, tick, snapshot);
    
    if (msg_type == ExchangeInterface::MessageType::BOOK) {
        std::cout << "\nParsed Order Book:" << std::endl;
        std::cout << "  Exchange: " << snapshot.exchange << std::endl;
        std::cout << "  Symbol: " << snapshot.symbol << std::endl;
        
        std::cout << "\n  Bids:" << std::endl;
        for (size_t i = 0; i < 5; ++i) {
            if (snapshot.bids[i].quantity > 0) {
                std::cout << "    " 
                          << snapshot.bids[i].price 
                          << " @ " 
                          << snapshot.bids[i].quantity 
                          << std::endl;
            }
        }
        
        std::cout << "\n  Asks:" << std::endl;
        for (size_t i = 0; i < 5; ++i) {
            if (snapshot.asks[i].quantity > 0) {
                std::cout << "    " 
                          << snapshot.asks[i].price 
                          << " @ " 
                          << snapshot.asks[i].quantity 
                          << std::endl;
            }
        }
    } else {
        std::cout << "Failed to parse order book message" << std::endl;
    }
}

/**
 * @brief Example 5: Full market data streaming (requires live connection)
 */
void example_live_streaming() {
    std::cout << "\n=== Example 5: Live Market Data Streaming ===" << std::endl;
    std::cout << "Note: This requires a live WebSocket connection\n" << std::endl;
    
    // Configure Hyperliquid exchange
    ExchangeConfig config;
    config.name = "hyperliquid";
    config.symbols = {"BTC", "ETH", "SOL"};
    config.enable_trades = true;
    config.enable_orderbook = true;
    config.snapshots_only = true;
    config.snapshot_interval = 1;
    config.reconnect_attempts = 10;
    config.reconnect_delay_ms = 5000;
    
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Exchange: " << config.name << std::endl;
    std::cout << "  Symbols: ";
    for (const auto& sym : config.symbols) {
        std::cout << sym << " ";
    }
    std::cout << std::endl;
    std::cout << "  Trades: " << (config.enable_trades ? "Yes" : "No") << std::endl;
    std::cout << "  Order Book: " << (config.enable_orderbook ? "Yes" : "No") << std::endl;
    
    // Uncomment to run live streaming:
    /*
    auto provider = std::make_unique<MarketDataProvider>(config);
    
    // Register callbacks
    provider->set_tick_callback(on_tick);
    provider->set_book_callback(on_book);
    
    // Start streaming
    provider->start();
    
    // Run for 60 seconds
    std::this_thread::sleep_for(std::chrono::seconds(60));
    
    // Stop streaming
    provider->stop();
    */
    
    std::cout << "\n(Uncomment the code block to enable live streaming)" << std::endl;
}

/**
 * @brief Main function - runs all examples
 */
int main(int argc, char* argv[]) {
    std::cout << "Hyperliquid Exchange Integration Examples" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    try {
        // Run examples
        example_basic_connection();
        example_subscription_generation();
        example_parse_trade_message();
        example_parse_orderbook_message();
        example_live_streaming();
        
        std::cout << "\n=== All Examples Completed ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
