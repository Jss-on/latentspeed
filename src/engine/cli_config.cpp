#include "engine/cli_config.h"
#include "core/auth/credentials_resolver.h"
#include <args.hxx>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <iostream>
#include <algorithm>
#include <filesystem>

namespace latentspeed {
namespace cli {

TradingEngineConfig parse_command_line_args(int argc, char* argv[]) {
    args::ArgumentParser parser("Latentspeed Trading Engine Service", 
                                "Ultra-low latency trading engine for cryptocurrency exchanges.");
    
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
    args::ValueFlag<std::string> exchange(parser, "name", "Exchange name (e.g., hyperliquid) [REQUIRED]", 
                                         {"exchange"});
    args::ValueFlag<std::string> api_key(parser, "key", "Exchange API key (DEX: wallet address)", 
                                        {"api-key"});
    args::ValueFlag<std::string> api_secret(parser, "secret", "Exchange API secret (DEX: private key)", 
                                           {"api-secret"});
    args::Flag live_trade(parser, "live-trade", "Enable live trading (default: demo/testnet)", 
                         {"live-trade"});
    args::Flag demo_mode(parser, "demo", "Use demo/testnet mode (default)", 
                        {"demo"});
    
    TradingEngineConfig config;
    
    try {
        parser.ParseCLI(argc, argv);
    } catch (const args::Completion& e) {
        std::cout << e.what();
        std::exit(0);
    } catch (const args::Help&) {
        std::cout << parser;
        std::cout << "\nExamples:\n";
        std::cout << "  # Demo trading (testnet)\n";
        std::cout << "  " << argv[0] << " --exchange hyperliquid --api-key YOUR_ADDRESS --api-secret YOUR_PRIVATE_KEY\n\n";
        std::cout << "  # Live trading\n";
        std::cout << "  " << argv[0] << " --exchange hyperliquid --api-key YOUR_ADDRESS --api-secret YOUR_PRIVATE_KEY --live-trade\n\n";
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

bool validate_config(const TradingEngineConfig& config) {
    std::vector<std::string> errors;
    
    if (config.exchange.empty()) {
        errors.push_back("--exchange is required");
    }
    
    // Check for supported exchanges
    std::vector<std::string> supported_exchanges = {"hyperliquid"};
    if (!config.exchange.empty() && 
        std::find(supported_exchanges.begin(), supported_exchanges.end(), config.exchange) == supported_exchanges.end()) {
        errors.push_back("Unsupported exchange '" + config.exchange + "'. Supported: hyperliquid");
    }

    // Credential resolution (env allowed) using centralized resolver
    if (errors.empty()) {
        auto creds = auth::resolve_credentials(config.exchange, config.api_key, config.api_secret, config.live_trade);
        if (creds.api_key.empty() || creds.api_secret.empty()) {
            if (config.exchange == "hyperliquid") {
                errors.push_back("Missing credentials: set --api-key/--api-secret (address/private key) or env LATENTSPEED_HYPERLIQUID_USER_ADDRESS / LATENTSPEED_HYPERLIQUID_PRIVATE_KEY");
            } else {
                std::string upper = config.exchange;
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                errors.push_back("Missing credentials: set --api-key/--api-secret or env LATENTSPEED_" + upper + "_API_KEY / LATENTSPEED_" + upper + "_API_SECRET");
            }
        }
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

bool initialize_logging() {
    // Create logs directory if it doesn't exist
    try {
        std::filesystem::create_directories("logs");
    } catch (const std::filesystem::filesystem_error& ex) {
        std::cerr << "Failed to create logs directory: " << ex.what() << std::endl;
        return false;
    }
    
    // Initialize spdlog with enhanced formatting
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
        
        return true;
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        return false;
    }
}

} // namespace cli
} // namespace latentspeed
