#pragma once

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <string_view>

namespace latentspeed::exec {

inline std::string canonical_reason_code(std::string_view raw_code) {
    std::string lower;
    lower.reserve(raw_code.size());
    std::transform(raw_code.begin(), raw_code.end(), std::back_inserter(lower), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (lower.empty() || lower == "ok" || lower == "accepted") {
        return std::string{"ok"};
    }

    if (lower == "invalid_params" || lower == "invalid_parameters" || lower == "invalid_parameter" ||
        lower == "missing_parameters" || lower == "missing_parameter" || lower == "missing_price" ||
        lower == "missing_stop_price" || lower == "missing_cancel_id" || lower == "missing_replace_id" ||
        lower == "missing_action" || lower == "invalid_action" || lower == "unsupported_type" ||
        lower == "invalid_size" || lower == "invalid_reduce_only" || lower == "parameter_error") {
        return std::string{"invalid_params"};
    }

    if (lower == "risk_blocked" || lower == "risk_violation") {
        return std::string{"risk_blocked"};
    }

    if (lower == "insufficient_balance" || lower == "balance_insufficient") {
        return std::string{"insufficient_balance"};
    }

    if (lower == "post_only_violation" || lower == "post_only_reject") {
        return std::string{"post_only_violation"};
    }

    if (lower == "min_size" || lower == "size_too_small") {
        return std::string{"min_size"};
    }

    if (lower == "price_out_of_bounds" || lower == "price_too_far") {
        return std::string{"price_out_of_bounds"};
    }

    if (lower == "rate_limited" || lower == "too_many_requests") {
        return std::string{"rate_limited"};
    }

    if (lower == "network_error" || lower == "exchange_error" || lower == "processing_error" ||
        lower == "timeout" || lower == "transport_error") {
        return std::string{"network_error"};
    }

    if (lower == "expired" || lower == "ttl_expired") {
        return std::string{"expired"};
    }

    if (lower == "venue_reject" || lower == "exchange_rejected" || lower == "cancel_rejected" ||
        lower == "modify_rejected" || lower == "close_rejected" || lower == "unknown_venue" ||
        lower == "close_denied" || lower == "cancel_denied") {
        return std::string{"venue_reject"};
    }

    return std::string{"venue_reject"};
}

}  // namespace latentspeed::exec
