/**
 * @file test_market_data.cpp
 * @brief Test application for market data provider using Boost.Beast
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#include "market_data_provider.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>

using namespace latentspeed;

// Global market data provider for signal handling
std::unique_ptr<MarketDataProvider> g_provider;
std::atomic<bool> g_shutdown{false};

void signal_handler(int signum) {
    spdlog::info("Received signal {}, shutting down...", signum);
    g_shutdown.store(true);
    if (g_provider) {
        g_provider->stop();
    }
}

class TestMarketDataCallback : public MarketDataCallbacks {
public:
    void on_trade(const MarketTick& tick) override {
        spdlog::info("[TRADE] {} {} @ {:.8f} x {:.8f} {} [{}]", 
                     tick.exchange.c_str(), tick.symbol.c_str(), 
                     tick.price, tick.quantity, tick.side.c_str(),
                     tick.trade_id.c_str());
        
        trade_count_++;
        if (trade_count_ % 100 == 0) {
            spdlog::info("[STATS] Processed {} trades, {} orderbooks", 
                         trade_count_.load(), orderbook_count_.load());
        }
    }
    
    void on_orderbook(const OrderBookSnapshot& snapshot) override {
        spdlog::info("[ORDERBOOK] {} {} - Bid: {:.8f}@{:.8f} | Ask: {:.8f}@{:.8f}",
                     snapshot.exchange.c_str(), snapshot.symbol.c_str(),
                     snapshot.bids[0].price, snapshot.bids[0].quantity,
                     snapshot.asks[0].price, snapshot.asks[0].quantity);
        
        orderbook_count_++;
    }
    
    void on_error(const std::string& error) override {
        spdlog::error("[ERROR] {}", error);
        error_count_++;
    }
    
    uint64_t get_trade_count() const { return trade_count_.load(); }
    uint64_t get_orderbook_count() const { return orderbook_count_.load(); }
    uint64_t get_error_count() const { return error_count_.load(); }

private:
    std::atomic<uint64_t> trade_count_{0};
    std::atomic<uint64_t> orderbook_count_{0};
    std::atomic<uint64_t> error_count_{0};
};

int main(int argc, char* argv[]) {
    // Setup logging
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S.%f] [%^%l%$] %v");
    
    spdlog::info("=== Market Data Provider Test (Boost.Beast) ===");
    
    // Parse command line arguments
    std::string exchange = "bybit";
    std::vector<std::string> symbols = {"BTCUSDT", "ETHUSDT"};
    
    if (argc >= 2) {
        exchange = argv[1];
    }
    if (argc >= 3) {
        symbols.clear();
        std::string symbols_str = argv[2];
        
        // Split comma-separated symbols
        size_t pos = 0;
        std::string token;
        while ((pos = symbols_str.find(',')) != std::string::npos) {
            token = symbols_str.substr(0, pos);
            if (!token.empty()) symbols.push_back(token);
            symbols_str.erase(0, pos + 1);
        }
        if (!symbols_str.empty()) symbols.push_back(symbols_str);
    }
    
    spdlog::info("Exchange: {}", exchange);
    spdlog::info("Symbols: [{}]", [&]() {
        std::string result;
        for (size_t i = 0; i < symbols.size(); ++i) {
            result += symbols[i];
            if (i < symbols.size() - 1) result += ", ";
        }
        return result;
    }());
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Create market data provider
        g_provider = std::make_unique<MarketDataProvider>(exchange, symbols);
        
        // Create callback handler
        auto callbacks = std::make_shared<TestMarketDataCallback>();
        g_provider->set_callbacks(callbacks);
        
        spdlog::info("Initializing market data provider...");
        if (!g_provider->initialize()) {
            spdlog::error("Failed to initialize market data provider");
            return 1;
        }
        
        spdlog::info("Starting market data streaming...");
        spdlog::info("ZMQ Ports: 5558 (trades), 5559 (orderbook)");
        spdlog::info("Using Boost.Beast WebSocket client");
        spdlog::info("Press Ctrl+C to stop");
        
        g_provider->start();
        
        // Statistics reporting thread
        std::thread stats_thread([&]() {
            auto start_time = std::chrono::steady_clock::now();
            
            while (!g_shutdown.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                
                if (g_shutdown.load()) break;
                
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                
                const auto& stats = g_provider->get_stats();
                auto trades = callbacks->get_trade_count();
                auto orderbooks = callbacks->get_orderbook_count();
                auto errors = callbacks->get_error_count();
                
                spdlog::info("=== STATS ({}s) ===", elapsed);
                spdlog::info("Trades processed: {} ({:.1f}/sec)", 
                             trades, elapsed > 0 ? static_cast<double>(trades) / elapsed : 0.0);
                spdlog::info("OrderBooks processed: {} ({:.1f}/sec)", 
                             orderbooks, elapsed > 0 ? static_cast<double>(orderbooks) / elapsed : 0.0);
                spdlog::info("Messages published: {}", stats.messages_published.load());
                spdlog::info("Errors: {}", errors);
            }
        });
        
        // Main loop
        while (!g_shutdown.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        stats_thread.join();
        
        spdlog::info("Stopping market data provider...");
        g_provider->stop();
        
        // Final statistics
        const auto& stats = g_provider->get_stats();
        spdlog::info("=== FINAL STATS ===");
        spdlog::info("Trades: {}", callbacks->get_trade_count());
        spdlog::info("OrderBooks: {}", callbacks->get_orderbook_count());
        spdlog::info("Published: {}", stats.messages_published.load());
        spdlog::info("Errors: {}", callbacks->get_error_count());
        
    } catch (const std::exception& e) {
        spdlog::error("Fatal error: {}", e.what());
        return 1;
    }
    
    spdlog::info("Market data test completed successfully");
    return 0;
}
