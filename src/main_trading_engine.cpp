/**
 * @file main_trading_engine.cpp
 * @brief Main entry point for the trading engine service
 * @author jessiondiwangan@gmail.com
 * @date 2025
 * 
 * Main function performs the complete service lifecycle:
 * 
 * **Initialization Phase:**
 * 1. Parses command line arguments (--exchange, --api-key, --api-secret, --live-trade)
 * 2. Validates configuration parameters
 * 3. Sets up signal handlers for graceful shutdown (SIGINT, SIGTERM)
 * 4. Creates TradingEngineService instance with configuration
 * 5. Initializes all components (ZeroMQ, exchange clients, market data feeds)
 * 
 * **Runtime Phase:**
 * 1. Starts all worker threads (order processing, market data, publishing)
 * 2. Enters main execution loop, monitoring service status
 * 3. Handles service lifecycle events and error conditions
 * 
 * **Shutdown Phase:**
 * 1. Responds to shutdown signals or service termination
 * 2. Ensures graceful shutdown of all worker threads
 * 3. Cleans up resources and exits
 * 
 * **Service Endpoints:**
 * - Orders: tcp://127.0.0.1:5601 (PULL socket for receiving ExecutionOrders)
 * - Reports: tcp://127.0.0.1:5602 (PUB socket for ExecutionReports and Fills)
 * - Market Data: tcp://127.0.0.1:5556/5557 (SUB sockets for preprocessed data)
 */

#include "trading_engine_service.h"
#include "engine/cli_config.h"
#include <spdlog/spdlog.h>
#include <signal.h>
#include <thread>
#include <chrono>
#include <iostream>

namespace latentspeed {

/**
 * @brief Global pointer to trading engine instance for signal handling
 */
TradingEngineService* g_trading_engine = nullptr;

/**
 * @brief Signal handler for graceful shutdown
 * @param signal The signal number received (SIGINT, SIGTERM, etc.)
 */
void signal_handler(int signal) {
    spdlog::info("\n[Main] Received signal {}, shutting down...", signal);
    if (g_trading_engine) {
        g_trading_engine->stop();
    }
}

} // namespace latentspeed

/**
 * @brief Main entry point
 */
int main(int argc, char* argv[]) {
    using namespace latentspeed;
    
    // Parse command line arguments
    TradingEngineConfig config;
    try {
        config = cli::parse_command_line_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Error parsing command line arguments: " << e.what() << std::endl;
        return 1;
    }
    
    // Validate configuration
    if (!cli::validate_config(config)) {
        std::cerr << "Use --help for usage information." << std::endl;
        return 1;
    }
    
    // Initialize logging
    if (!cli::initialize_logging()) {
        std::cerr << "Failed to initialize logging system" << std::endl;
        return 1;
    }
    
    spdlog::info("=== Latentspeed Trading Engine Service ===");
    spdlog::info("Starting up...");

    // Set up signal handling for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    try {
        // Display configuration summary
        spdlog::info("[Main] Configuration Summary:");
        spdlog::info("[Main]   Exchange: {}", config.exchange);
        spdlog::info("[Main]   Trading Mode: {}", config.live_trade ? "LIVE" : "DEMO/TESTNET");
        spdlog::info("[Main]   API Key: {}...{} (masked)", 
                     config.api_key.substr(0, 4), 
                     config.api_key.length() > 8 ? config.api_key.substr(config.api_key.length() - 4) : "");
        
        // Create and initialize trading engine with configuration
        TradingEngineService trading_engine(config);
        g_trading_engine = &trading_engine;

        if (!trading_engine.initialize()) {
            spdlog::error("[Main] Failed to initialize trading engine");
            return 1;
        }

        // Start the service
        trading_engine.start();

        spdlog::info("[Main] Trading engine started successfully");
        spdlog::info("[Main] Listening for orders on tcp://127.0.0.1:5601");
        spdlog::info("[Main] Publishing reports on tcp://127.0.0.1:5602");
        spdlog::info("[Main] Press Ctrl+C to stop");

        // Main loop - keep the service running
        while (trading_engine.is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        spdlog::info("[Main] Trading engine stopped");

    } catch (const std::exception& e) {
        spdlog::error("[Main] Fatal error: {}", e.what());
        return 1;
    }

    return 0;
}
