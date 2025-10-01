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
#include <vector>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <args.hxx>

/**
 * @brief Global pointer to trading engine instance for signal handling
 * 
 * Used by the signal handler to gracefully shutdown the service when
 * receiving termination signals (Ctrl+C, SIGTERM, etc.)
 */
latentspeed::TradingEngineService* g_trading_engine = nullptr;

/**
 * @brief Parse command line arguments using args library
 * @param argc Argument count
 * @param argv Argument vector
 * @return TradingEngineConfig with parsed values
 */
latentspeed::TradingEngineConfig parse_command_line_args(int argc, char* argv[]) {
    args::ArgumentParser parser("Latentspeed Trading Engine Service", 
                                "Ultra-low latency trading engine for cryptocurrency exchanges.");
    
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
    args::ValueFlag<std::string> exchange(parser, "name", "Exchange name (e.g., bybit) [REQUIRED]", 
                                         {"exchange"});
    args::ValueFlag<std::string> api_key(parser, "key", "Exchange API key [REQUIRED]", 
                                        {"api-key"});
    args::ValueFlag<std::string> api_secret(parser, "secret", "Exchange API secret [REQUIRED]", 
                                           {"api-secret"});
    args::Flag live_trade(parser, "live-trade", "Enable live trading (default: demo/testnet)", 
                         {"live-trade"});
    args::Flag demo_mode(parser, "demo", "Use demo/testnet mode (default)", 
                        {"demo"});
    
    latentspeed::TradingEngineConfig config;
    
    try {
        parser.ParseCLI(argc, argv);
    } catch (const args::Completion& e) {
        std::cout << e.what();
        std::exit(0);
    } catch (const args::Help&) {
        std::cout << parser;
        std::cout << "\nExamples:\n";
        std::cout << "  # Demo trading (testnet)\n";
        std::cout << "  " << argv[0] << " --exchange bybit --api-key YOUR_KEY --api-secret YOUR_SECRET\n\n";
        std::cout << "  # Live trading\n";
        std::cout << "  " << argv[0] << " --exchange bybit --api-key YOUR_KEY --api-secret YOUR_SECRET --live-trade\n\n";
        std::exit(0);
    } catch (const args::ParseError& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        std::exit(1);
    }
    
    // Extract values
    if (exchange) config.exchange = args::get(exchange);
    if (api_key) config.api_key = args::get(api_key);
    if (api_secret) config.api_secret = args::get(api_secret);
    
    // Handle trading mode flags
    if (live_trade && demo_mode) {
        std::cerr << "Error: Cannot specify both --live-trade and --demo\n";
        std::exit(1);
    }
    
    config.live_trade = live_trade ? true : false;
    
    return config;
}

/**
 * @brief Validate command line configuration
 * @param config Configuration to validate
 * @return true if valid, false otherwise
 */
bool validate_config(const latentspeed::TradingEngineConfig& config) {
    std::vector<std::string> errors;
    
    if (config.exchange.empty()) {
        errors.push_back("--exchange is required");
    }
    
    if (config.api_key.empty()) {
        errors.push_back("--api-key is required");
    }
    
    if (config.api_secret.empty()) {
        errors.push_back("--api-secret is required");
    }
    
    // Check for supported exchanges
    std::vector<std::string> supported_exchanges = {"bybit"};
    if (!config.exchange.empty() && 
        std::find(supported_exchanges.begin(), supported_exchanges.end(), config.exchange) == supported_exchanges.end()) {
        errors.push_back("Unsupported exchange '" + config.exchange + "'. Supported: bybit");
    }
    
    if (!errors.empty()) {
        std::cerr << "Configuration errors:\n";
        for (const auto& error : errors) {
            std::cerr << "  - " << error << "\n";
        }
        return false;
    }
    
    return true;
}

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
 * @param argc Argument count
 * @param argv Argument vector containing command line arguments
 * @return Exit code: 0 for success, 1 for failure
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
 * 
 * **Error Handling:**
 * All exceptions are caught and logged with appropriate error messages.
 * The service returns non-zero exit codes on failure for proper system integration.
 */
int main(int argc, char* argv[]) {
    // Parse command line arguments
    latentspeed::TradingEngineConfig config;
    try {
        config = parse_command_line_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Error parsing command line arguments: " << e.what() << std::endl;
        return 1;
    }
    
    // Validate configuration
    if (!validate_config(config)) {
        std::cerr << "Use --help for usage information." << std::endl;
        return 1;
    }
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
        // Display configuration summary
        spdlog::info("[Main] Configuration Summary:");
        spdlog::info("[Main]   Exchange: {}", config.exchange);
        spdlog::info("[Main]   Trading Mode: {}", config.live_trade ? "LIVE" : "DEMO/TESTNET");
        spdlog::info("[Main]   API Key: {}...{} (masked)", 
                     config.api_key.substr(0, 4), 
                     config.api_key.length() > 8 ? config.api_key.substr(config.api_key.length() - 4) : "");
        
        // Create and initialize trading engine with configuration
        latentspeed::TradingEngineService trading_engine(config);
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
