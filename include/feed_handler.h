/**
 * @file feed_handler.h
 * @brief Multi-exchange feed handler (similar to cryptofeed's FeedHandler)
 * @author jessiondiwangan@gmail.com
 * @date 2025
 * 
 * Manages multiple exchange connections concurrently with independent
 * WebSocket streams per exchange.
 */

#pragma once

#include "exchange_interface.h"
#include "market_data_provider.h"
#include <vector>
#include <memory>
#include <thread>
#include <atomic>

namespace latentspeed {

/**
 * @class FeedHandler
 * @brief Manages multiple exchange feeds concurrently
 * 
 * Architecture:
 * - One MarketDataProvider instance per exchange
 * - Each provider runs in its own thread context
 * - All feeds publish to the same ZMQ ports (5556/5557)
 * - Exchange name in topic disambiguates sources
 */
class FeedHandler {
public:
    /**
     * @brief Feed handler configuration
     */
    struct Config {
        bool backend_multiprocessing = false;  ///< Not used in C++ (always multi-threaded)
        int zmq_trades_port = 5556;           ///< ZMQ trades port
        int zmq_books_port = 5557;            ///< ZMQ orderbook port
        int window_size = 20;                 ///< Rolling statistics window size
        int depth_levels = 10;                ///< Orderbook depth levels
    };
    
    // Default constructor
    FeedHandler()
        : config_{}
        , running_(false) {}
    
    // Constructor with config
    explicit FeedHandler(const Config& config)
        : config_(config)
        , running_(false) {}
    
    ~FeedHandler() {
        stop();
    }
    
    /**
     * @brief Add an exchange feed
     * @param exchange_config Exchange configuration
     * @param callbacks Optional callbacks for this feed
     */
    void add_feed(
        const ExchangeConfig& exchange_config,
        std::shared_ptr<MarketDataCallbacks> callbacks = nullptr
    );
    
    /**
     * @brief Start all feeds
     */
    void start();
    
    /**
     * @brief Stop all feeds gracefully
     */
    void stop();
    
    /**
     * @brief Get statistics from all feeds
     */
    struct FeedStats {
        std::string exchange;
        uint64_t messages_received;
        uint64_t messages_published;
        uint64_t errors;
    };
    
    std::vector<FeedStats> get_stats() const;
    
    /**
     * @brief Check if handler is running
     */
    bool is_running() const { return running_.load(); }
    
    /**
     * @brief Get number of active feeds
     */
    size_t num_feeds() const { return providers_.size(); }

private:
    struct FeedEntry {
        ExchangeConfig config;
        std::unique_ptr<ExchangeInterface> exchange;
        std::unique_ptr<MarketDataProvider> provider;
        std::shared_ptr<MarketDataCallbacks> callbacks;
    };
    
    Config config_;
    std::vector<FeedEntry> providers_;
    std::atomic<bool> running_;
};

/**
 * @class ConfigLoader
 * @brief Load feed configuration from YAML file (similar to Python config.yml)
 */
class ConfigLoader {
public:
    /**
     * @brief Load configuration from YAML file
     * @param path Path to config.yml
     * @return FeedHandler config and list of exchange configs
     */
    struct LoadedConfig {
        FeedHandler::Config handler_config;
        std::vector<ExchangeConfig> feeds;
    };
    
    static LoadedConfig load_from_yaml(const std::string& path);
    
    /**
     * @brief Create example config file
     */
    static void create_example_config(const std::string& path);
};

} // namespace latentspeed
