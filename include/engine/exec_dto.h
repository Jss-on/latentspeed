/**
 * @file exec_dto.h
 * @brief Phase 2: Typed ExecutionOrder DTO parser for engine input.
 */

#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <map>
#include <cstdint>

namespace latentspeed {

struct ExecParsedDetails {
    std::string symbol;
    std::string side;
    std::string order_type;
    std::string time_in_force;
    std::optional<double> price;
    std::optional<double> size;
    std::optional<double> stop_price;
    std::optional<bool> reduce_only;
    std::map<std::string,std::string> params;   // flat primitives only
    std::map<std::string,std::string> cancel;   // flat primitives only
    std::map<std::string,std::string> replace;  // flat primitives only
};

struct ExecParsed {
    int version{1};
    std::string cl_id;
    std::string action;
    std::string venue_type;
    std::string venue;
    std::string product_type;
    uint64_t ts_ns{0};
    ExecParsedDetails details;
    std::map<std::string,std::string> tags;   // flat primitives only
};

// Parse ExecutionOrder JSON into ExecParsed (flat primitive fields only).
// Returns true on success.
bool parse_exec_order_json(std::string_view json, ExecParsed& out);

} // namespace latentspeed
