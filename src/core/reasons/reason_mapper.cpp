/**
 * @file reason_mapper.cpp
 */

#include "core/reasons/reason_mapper.h"
#include <algorithm>

namespace latentspeed {

static inline std::string lower_ascii(std::string_view s) {
    std::string out(s.begin(), s.end());
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string DefaultReasonMapper::canonical_code(std::string_view raw_code) const {
    std::string lower = lower_ascii(raw_code);
    if (lower.empty() || lower == "ok" || lower == "accepted") return "ok";
    if (lower == "invalid_params" || lower == "invalid_parameters" || lower == "invalid_parameter" ||
        lower == "missing_parameters" || lower == "missing_parameter" || lower == "missing_price" ||
        lower == "missing_stop_price" || lower == "missing_cancel_id" || lower == "missing_replace_id" ||
        lower == "missing_action" || lower == "invalid_action" || lower == "unsupported_type" ||
        lower == "invalid_size" || lower == "invalid_reduce_only" || lower == "parameter_error") {
        return "invalid_params";
    }
    if (lower == "risk_blocked" || lower == "risk_violation" || lower == "perpmaxpositionrejected") return "risk_blocked";
    if (lower == "insufficient_balance" || lower == "balance_insufficient") return "insufficient_balance";
    if (lower == "post_only_violation" || lower == "post_only_reject" || lower == "badalopxrejected") return "post_only_violation";
    if (lower == "min_size" || lower == "size_too_small" || lower == "mintradentlrejected" || lower == "mintradespotntlrejected") return "min_size";
    if (lower == "price_out_of_bounds" || lower == "price_too_far" || lower == "tickrejected" || lower == "oraclerejected") return "price_out_of_bounds";
    if (lower == "rate_limited" || lower == "too_many_requests") return "rate_limited";
    if (lower == "network_error" || lower == "exchange_error" || lower == "processing_error" ||
        lower == "timeout" || lower == "transport_error") return "network_error";
    if (lower == "expired" || lower == "ttl_expired") return "expired";
    if (lower == "venue_reject" || lower == "exchange_rejected" || lower == "cancel_rejected" ||
        lower == "modify_rejected" || lower == "close_rejected" || lower == "unknown_venue" ||
        lower == "close_denied" || lower == "cancel_denied") return "venue_reject";
    return "venue_reject";
}

ReasonMapping DefaultReasonMapper::map(std::string_view normalized_status,
                                       std::string_view raw_reason) const {
    ReasonMapping r;
    r.status = std::string(normalized_status);
    if (r.status == "rejected") {
        std::string lower = lower_ascii(raw_reason);
        if (lower.empty()) {
            r.reason_code = "venue_reject";
            r.reason_text = "Order rejected";
            return r;
        }
        // Hyperliquid-specific mappings (lowercased reason/status tag)
        if (lower.find("balance") != std::string::npos || lower == "insufficientspotbalancerejected") {
            r.reason_code = "insufficient_balance";
            r.reason_text = raw_reason.empty() ? "Insufficient balance" : std::string(raw_reason);
            return r;
        }
        if (lower == "tickrejected" || lower == "oraclerejected") {
            r.reason_code = "price_out_of_bounds";
            r.reason_text = "Rejected by tick/oracle constraint";
            return r;
        }
        if (lower == "mintradentlrejected" || lower == "mintradespotntlrejected") {
            r.reason_code = "min_size";
            r.reason_text = "Order notional below minimum";
            return r;
        }
        if (lower == "badalopxrejected") {
            r.reason_code = "post_only_violation";
            r.reason_text = "Post-only would match immediately";
            return r;
        }
        if (lower == "perpmaxpositionrejected") {
            r.reason_code = "risk_blocked";
            r.reason_text = "Position exceeds margin tier limit";
            return r;
        }
        if (lower == "perpmarginrejected") {
            r.reason_code = "insufficient_balance";
            r.reason_text = "Insufficient margin";
            return r;
        }
        if (lower == "reduceonlyrejected") {
            r.reason_code = "invalid_params";
            r.reason_text = "Reduce-only would increase position";
            return r;
        }
        if (lower == "ioccancelrejected" || lower == "marketordernoliquidityrejected") {
            r.reason_code = "venue_reject";
            r.reason_text = "No liquidity for immediate execution";
            return r;
        }
        // Open interest cap variants
        if (lower.find("openinterest") != std::string::npos || lower.find("to oaggressive") != std::string::npos) {
            r.reason_code = "venue_reject";
            r.reason_text = raw_reason.empty() ? "Open interest cap" : std::string(raw_reason);
            return r;
        }
        // Default
        r.reason_code = "venue_reject";
        r.reason_text = raw_reason.empty() ? "Order rejected" : std::string(raw_reason);
        return r;
    }
    if (r.status == "canceled") {
        r.reason_code = "ok";
        r.reason_text = "Order cancelled";
        return r;
    }
    if (r.status == "replaced") {
        r.reason_code = "ok";
        r.reason_text = "Order replaced";
        return r;
    }
    r.reason_code = "ok";
    r.reason_text = raw_reason.empty() ? "OK" : std::string(raw_reason);
    return r;
}

} // namespace latentspeed
