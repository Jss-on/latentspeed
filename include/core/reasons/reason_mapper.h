/**
 * @file reason_mapper.h
 * @brief Phase 2: Canonical reason mapping interface.
 */

#pragma once

#include <string>
#include <string_view>

namespace latentspeed {

struct ReasonMapping {
    std::string status;      // normalized status: accepted/canceled/rejected/replaced
    std::string reason_code; // canonical reason code per contract
    std::string reason_text; // human-readable text
};

class IReasonMapper {
public:
    virtual ~IReasonMapper() = default;
    virtual std::string canonical_code(std::string_view raw_code) const = 0;
    virtual ReasonMapping map(std::string_view normalized_status,
                              std::string_view raw_reason) const = 0;
};

// Default mapper uses existing helpers from trading_engine_service.cpp behavior
class DefaultReasonMapper : public IReasonMapper {
public:
    std::string canonical_code(std::string_view raw_code) const override;
    ReasonMapping map(std::string_view normalized_status,
                      std::string_view raw_reason) const override;
};

} // namespace latentspeed

