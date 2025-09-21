/**
 * @file main.cpp
 * @brief Main entry point for the Latentspeed Trading Engine Service
 * @author Jession Diwangan
 * @date 2025
 * 
 * This file contains the main function and service lifecycle management
 * for the trading engine. It handles:
 * - Service initialization and startup
 * - Graceful shutdown on system signals (SIGINT, SIGTERM)
 * - Error handling and logging
 * - Main execution loop
 */

#include "trading_engine_service.h"
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

/**
 * @brief Global pointer to trading engine instance for signal handling
 * 
 * Used by the signal handler to gracefully shutdown the service when
 * receiving termination signals (Ctrl+C, SIGTERM, etc.)
 */
latentspeed::TradingEngineService* g_trading_engine = nullptr;

/**
 * @brief Signal handler for graceful shutdown
 * @param signal The signal number received (SIGINT, SIGTERM, etc.)
 * 
 * Handles system signals by:
 * 1. Logging the received signal
 * 2. Calling stop() on the trading engine instance
 * 3. Allowing the main loop to exit cleanly
 * 
 * This ensures that all worker threads are properly shut down,
 * pending orders are handled appropriately, and resources are cleaned up.
 */
void signal_handler(int signal) {
    spdlog::info("\n[Main] Received signal {}, shutting down...", signal);
    if (g_trading_engine) {
        g_trading_engine->stop();
    }
}

/**
 * @brief Main entry point for the trading engine service
 * @param argc Argument count (currently unused)
 * @param argv Argument vector (currently unused)
 * @return Exit code: 0 for success, 1 for failure
 * 
 * Main function performs the complete service lifecycle:
 * 
 * **Initialization Phase:**
 * 1. Sets up signal handlers for graceful shutdown (SIGINT, SIGTERM)
 * 2. Creates TradingEngineService instance
 * 3. Initializes all components (ZeroMQ, ccapi, market data feeds)
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
 * 
 * **Error Handling:**
 * All exceptions are caught and logged with appropriate error messages.
 * The service returns non-zero exit codes on failure for proper system integration.
 */
int main(int argc, char* argv[]) {
    // Create logs directory if it doesn't exist
    try {
        std::filesystem::create_directories("logs");
    } catch (const std::filesystem::filesystem_error& ex) {
        std::cerr << "Failed to create logs directory: " << ex.what() << std::endl;
        return 1;
    }
    
    // Initialize spdlog with enhanced formatting including timestamp, thread ID, function name
    try {
        // Create console sink with colors
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::debug);
        
        // Create rotating file sink for persistent logging
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/trading_engine.log", 1024*1024*5, 3);
        file_sink->set_level(spdlog::level::trace);
        
        // Combine sinks
        std::vector<spdlog::sink_ptr> sinks {console_sink, file_sink};
        auto logger = std::make_shared<spdlog::logger>("multi_sink", sinks.begin(), sinks.end());
        
        // Enhanced pattern with timestamp, thread ID, process ID, source location, function name
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [PID:%P] [TID:%t] [%^%l%$] [%s:%#] [%!] %v");
        logger->set_level(spdlog::level::info);
        
        spdlog::set_default_logger(logger);
        spdlog::flush_every(std::chrono::seconds(1));
        
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        return 1;
    }
    
    spdlog::info("=== Latentspeed Trading Engine Service ===");
    spdlog::info("Starting up...");

    // Set up signal handling for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    try {
        // Create and initialize trading engine
        latentspeed::TradingEngineService trading_engine(latentspeed::CpuMode::ECO);
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

    spdlog::info("[Main] Shutdown complete");
    return 0;
}
