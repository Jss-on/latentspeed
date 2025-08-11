#include "trading_engine_service.h"
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>

// Global pointer for signal handling
latentspeed::TradingEngineService* g_trading_engine = nullptr;

void signal_handler(int signal) {
    std::cout << "\n[Main] Received signal " << signal << ", shutting down..." << std::endl;
    if (g_trading_engine) {
        g_trading_engine->stop();
    }
}

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
        std::cout << "[Main] Listening for strategy commands on tcp://*:5555" << std::endl;
        std::cout << "[Main] Broadcasting market data on tcp://*:5556" << std::endl;
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
