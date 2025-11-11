#pragma once

#include "hft_data_structures.h"
#include <string>

namespace latentspeed {
namespace engine {

/**
 * @brief Serialize HFT execution report to JSON
 */
std::string serialize_execution_report(const hft::HFTExecutionReport& report);

/**
 * @brief Serialize HFT fill to JSON
 */
std::string serialize_fill(const hft::HFTFill& fill);

} // namespace engine
} // namespace latentspeed
