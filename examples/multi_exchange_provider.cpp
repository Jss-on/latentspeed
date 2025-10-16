/**
 * @file multi_exchange_provider.cpp
 * @brief Example: Multi-exchange market data provider
 * @author jessiondiwangan@gmail.com
 * @date 2025
 * 
 * Demonstrates how to connect to multiple exchanges simultaneously,
 * similar to Python cryptofeed's FeedHandler.
 */

#include "feed_handler.h"
#include "exchange_interface.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <signal.h>
#include <iostream>

using namespace latentspeed;

// Global feed handler for signal handling
std::unique_ptr<FeedHandler> g_feed_handler;
std::atomic<bool> g_shutdown{false};

void signal_handler(int signum) {
    spdlog::info("Received signal {}, shutting down...", signum);
    g_shutdown.store(true);
    if (g_feed_handler) {
        g_feed_handler->stop();
    }
}

// Custom callback to log all messages
class LoggingCallback : public MarketDataCallbacks {
public:
    void on_trade(const MarketTick& tick) override {
        spdlog::info("[TRADE] {} {} @ {:.8f} x {:.8f} {}",
                     tick.exchange.c_str(), tick.symbol.c_str(),
                     tick.price, tick.amount, tick.side.c_str());
        
        trade_count_++;
        if (trade_count_ % 100 == 0) {
            spdlog::info("[STATS] Processed {} trades, {} orderbooks",
                         trade_count_.load(), orderbook_count_.load());
        }
    }
    
    void on_orderbook(const OrderBookSnapshot& snapshot) override {
        spdlog::info("[BOOK] {} {} - Mid: {:.8f} Spread: {:.6f}% Vol: {:.2f} OFI: {:.4f}",
                     snapshot.exchange.c_str(), snapshot.symbol.c_str(),
                     snapshot.midpoint, snapshot.relative_spread * 100,
                     snapshot.volatility_mid, snapshot.ofi_rolling);
        
        orderbook_count_++;
    }
    
    void on_error(const std::string& error) override {
        spdlog::error("[ERROR] {}", error);
    }
    
private:
    std::atomic<uint64_t> trade_count_{0};
    std::atomic<uint64_t> orderbook_count_{0};
};

int main(int argc, char** argv) {
    // Setup logging
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    
    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    spdlog::info("============================================================");
    spdlog::info("Multi-Exchange Market Data Provider");
    spdlog::info("Similar to Python cryptofeed FeedHandler");
    spdlog::info("============================================================");
    
    try {
        // Option 1: Load from config file
        bool use_config_file = (argc > 1 && std::string(argv[1]) == "--config");
        
        if (use_config_file) {
            std::string config_path = argc > 2 ? argv[2] : "config.yml";
            
            spdlog::info("Loading configuration from: {}", config_path);
            auto config = ConfigLoader::load_from_yaml(config_path);
            
            // Create feed handler
            g_feed_handler = std::make_unique<FeedHandler>(config.handler_config);
            
            // Add all feeds from config
            auto callbacks = std::make_shared<LoggingCallback>();
            for (const auto& feed_config : config.feeds) {
                g_feed_handler->add_feed(feed_config, callbacks);
            }
            
        } else {
            // Option 2: Programmatic configuration
            spdlog::info("Using programmatic configuration");
            
            FeedHandler::Config handler_config;
            handler_config.zmq_trades_port = 5556;
            handler_config.zmq_books_port = 5557;
            handler_config.window_size = 20;
            
            g_feed_handler = std::make_unique<FeedHandler>(handler_config);
            
            // Create shared callback
            auto callbacks = std::make_shared<LoggingCallback>();
            
            // Add Bybit feed
            ExchangeConfig bybit_config;
            bybit_config.name = "bybit";
            bybit_config.symbols = {"BTC-USDT", "ETH-USDT", "SOL-USDT"};
            bybit_config.enable_trades = true;
            bybit_config.enable_orderbook = true;
            bybit_config.snapshots_only = true;
            
            g_feed_handler->add_feed(bybit_config, callbacks);
            
            // Add Binance feed
            ExchangeConfig binance_config;
            binance_config.name = "binance";
            binance_config.symbols = {"BTC-USDT", "ETH-USDT"};
            binance_config.enable_trades = true;
            binance_config.enable_orderbook = true;
            binance_config.snapshots_only = true;
            
            g_feed_handler->add_feed(binance_config, callbacks);
            
            // Add dYdX feed
            ExchangeConfig dydx_config;
            dydx_config.name = "dydx";
            dydx_config.symbols = {"BTC-USD", "ETH-USD"};  // dYdX uses USD not USDT
            dydx_config.enable_trades = true;
            dydx_config.enable_orderbook = true;
            dydx_config.snapshots_only = true;
            
            g_feed_handler->add_feed(dydx_config, callbacks);
            
            spdlog::info("Added {} feeds", g_feed_handler->num_feeds());
        }
        
        // Start all feeds
        spdlog::info("Starting feed handler...");
        g_feed_handler->start();
        
        spdlog::info("============================================================");
        spdlog::info("Feed handler running. Press Ctrl+C to stop.");
        spdlog::info("ZMQ Ports: 5556 (trades), 5557 (orderbooks)");
        spdlog::info("============================================================");
        
        // Statistics reporting loop
        while (!g_shutdown.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            
            if (!g_shutdown.load()) {
                spdlog::info("--- Statistics Report ---");
                auto stats = g_feed_handler->get_stats();
                for (const auto& feed_stats : stats) {
                    spdlog::info("{}: {} msgs received, {} published, {} errors",
                                 feed_stats.exchange,
                                 feed_stats.messages_received,
                                 feed_stats.messages_published,
                                 feed_stats.errors);
                }
                spdlog::info("------------------------");
            }
        }
        
        spdlog::info("Shutting down gracefully...");
        g_feed_handler->stop();
        
    } catch (const std::exception& e) {
        spdlog::error("Fatal error: {}", e.what());
        return 1;
    }
    
    spdlog::info("Shutdown complete");
    return 0;
}
