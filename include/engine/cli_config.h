#pragma once

#include "trading_engine_service.h"
#include <string>

namespace latentspeed {
namespace cli {

/**
 * @brief Parse command line arguments
 * @param argc Argument count
 * @param argv Argument vector
 * @return Parsed configuration
 */
TradingEngineConfig parse_command_line_args(int argc, char* argv[]);

/**
 * @brief Validate configuration
 * @param config Configuration to validate
 * @return true if valid, false otherwise
 */
bool validate_config(const TradingEngineConfig& config);

/**
 * @brief Initialize logging system
 * @return true if successful, false otherwise
 */
bool initialize_logging();

} // namespace cli
} // namespace latentspeed
