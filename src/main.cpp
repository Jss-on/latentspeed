/**
 * @file main.cpp
 * @brief Main entry point for the Latentspeed Trading Engine Service
 * @author Latentspeed Trading Team
 * @date 2024
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
    std::cout << "\n[Main] Received signal " << signal << ", shutting down..." << std::endl;
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
    std::cout << "=== Latentspeed Trading Engine Service ===" << std::endl;
    std::cout << "Starting up..." << std::endl;

    // Set up signal handling for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    try {
        // Create and initialize trading engine
        latentspeed::TradingEngineService trading_engine;
        g_trading_engine = &trading_engine;

        if (!trading_engine.initialize()) {
            std::cerr << "[Main] Failed to initialize trading engine" << std::endl;
            return 1;
        }

        // Start the service
        trading_engine.start();

        std::cout << "[Main] Trading engine started successfully" << std::endl;
        std::cout << "[Main] Listening for orders on tcp://127.0.0.1:5601" << std::endl;
        std::cout << "[Main] Publishing reports on tcp://127.0.0.1:5602" << std::endl;
        std::cout << "[Main] Press Ctrl+C to stop" << std::endl;

        // Main loop - keep the service running
        while (trading_engine.is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "[Main] Trading engine stopped" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[Main] Fatal error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "[Main] Shutdown complete" << std::endl;
    return 0;
}
