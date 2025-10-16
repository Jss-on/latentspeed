/**
 * @file dydx_provider.cpp
 * @brief Simple dYdX market data provider using FeedHandler
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#include "feed_handler.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <signal.h>

using namespace latentspeed;

// Global feed handler
std::unique_ptr<FeedHandler> g_feed_handler;
std::atomic<bool> g_shutdown{false};

void signal_handler(int signum) {
    spdlog::info("Shutting down...");
    g_shutdown.store(true);
    if (g_feed_handler) {
        g_feed_handler->stop();
    }
}

// Simple callback to log dYdX data
class DydxCallback : public MarketDataCallbacks {
public:
    void on_trade(const MarketTick& tick) override {
        spdlog::info("[TRADE] {} @ ${:.2f} x {:.4f} {}",
                     tick.symbol.c_str(), tick.price, tick.amount, tick.side.c_str());
        trade_count_++;
    }
    
    void on_orderbook(const OrderBookSnapshot& snapshot) override {
        spdlog::info("[BOOK] {} - Mid: ${:.2f} Spread: {:.2f} bps",
                     snapshot.symbol.c_str(), 
                     snapshot.midpoint,
                     snapshot.relative_spread * 10000);
        book_count_++;
    }
    
    void on_error(const std::string& error) override {
        spdlog::error("[ERROR] {}", error);
    }
    
    uint64_t get_trade_count() const { return trade_count_.load(); }
    uint64_t get_book_count() const { return book_count_.load(); }
    
private:
    std::atomic<uint64_t> trade_count_{0};
    std::atomic<uint64_t> book_count_{0};
};

int main(int argc, char** argv) {
    // Setup logging
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::set_level(spdlog::level::debug);  // Enable debug to see all messages
    
    // Signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    spdlog::info("===========================================");
    spdlog::info("dYdX Market Data Provider");
    spdlog::info("===========================================");
    
    try {
        // Configure FeedHandler
        FeedHandler::Config config;
        config.zmq_trades_port = 5556;
        config.zmq_books_port = 5557;
        config.window_size = 20;
        
        g_feed_handler = std::make_unique<FeedHandler>(config);
        
        // Parse symbols from command line or use defaults
        std::vector<std::string> symbols = {"BTC-USD", "ETH-USD"};
        
        if (argc > 1) {
            symbols.clear();
            for (int i = 1; i < argc; ++i) {
                symbols.push_back(argv[i]);
            }
        }
        
        spdlog::info("Symbols: {}", [&]() {
            std::string result;
            for (size_t i = 0; i < symbols.size(); ++i) {
                result += symbols[i];
                if (i < symbols.size() - 1) result += ", ";
            }
            return result;
        }());
        
        // Configure dYdX feed
        ExchangeConfig dydx_config;
        dydx_config.name = "dydx";
        dydx_config.symbols = symbols;
        dydx_config.enable_trades = true;
        dydx_config.enable_orderbook = true;
        dydx_config.snapshots_only = true;
        dydx_config.snapshot_interval = 1;
        
        // Add callback
        auto callbacks = std::make_shared<DydxCallback>();
        
        // Add dYdX feed
        g_feed_handler->add_feed(dydx_config, callbacks);
        
        // Start
        spdlog::info("Starting dYdX feed...");
        g_feed_handler->start();
        
        spdlog::info("===========================================");
        spdlog::info("Streaming dYdX data (Press Ctrl+C to stop)");
        spdlog::info("ZMQ Ports: 5556 (trades), 5557 (orderbooks)");
        spdlog::info("===========================================\n");
        
        // Stats loop
        auto start_time = std::chrono::steady_clock::now();
        
        while (!g_shutdown.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            
            if (!g_shutdown.load()) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                
                spdlog::info("--- Stats ({}s) ---", elapsed);
                spdlog::info("Trades: {} ({:.1f}/sec)", 
                           callbacks->get_trade_count(),
                           elapsed > 0 ? static_cast<double>(callbacks->get_trade_count()) / elapsed : 0.0);
                spdlog::info("Books: {} ({:.1f}/sec)", 
                           callbacks->get_book_count(),
                           elapsed > 0 ? static_cast<double>(callbacks->get_book_count()) / elapsed : 0.0);
            }
        }
        
        spdlog::info("\nStopping...");
        g_feed_handler->stop();
        
    } catch (const std::exception& e) {
        spdlog::error("Fatal error: {}", e.what());
        return 1;
    }
    
    spdlog::info("Shutdown complete");
    return 0;
}
