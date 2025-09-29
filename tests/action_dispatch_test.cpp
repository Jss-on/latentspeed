#include "action_dispatch.h"
#include "reason_code_mapper.h"

#include <cstdio>
#include <cstdlib>

using latentspeed::dispatch::ActionKind;
using latentspeed::dispatch::decode_action;
using latentspeed::dispatch::fnv1a_32;
using latentspeed::dispatch::kCancelHash;
using latentspeed::dispatch::kPlaceHash;
using latentspeed::dispatch::kReplaceHash;
using latentspeed::exec::canonical_reason_code;

namespace {

template <typename T>
int check(const T& condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[dispatch-test] %s\n", message);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

}  // namespace

int main() {
    if (int rc = check(fnv1a_32("place") == kPlaceHash, "hash mismatch for place")) {
        return rc;
    }
    if (int rc = check(fnv1a_32("cancel") == kCancelHash, "hash mismatch for cancel")) {
        return rc;
    }
    if (int rc = check(fnv1a_32("replace") == kReplaceHash, "hash mismatch for replace")) {
        return rc;
    }

    if (int rc = check(decode_action("place") == ActionKind::Place, "decode place failed")) {
        return rc;
    }
    if (int rc = check(decode_action("cancel") == ActionKind::Cancel, "decode cancel failed")) {
        return rc;
    }
    if (int rc = check(decode_action("replace") == ActionKind::Replace, "decode replace failed")) {
        return rc;
    }
    if (int rc = check(decode_action("unknown") == ActionKind::Unknown, "decode unknown failed")) {
        return rc;
    }

    // Raw uppercase should not map without normalization; decode_action expects lowercase
    if (int rc = check(decode_action("PLACE") == ActionKind::Unknown, "decode uppercase should remain unknown")) {
        return rc;
    }

    if (int rc = check(canonical_reason_code("cancel_rejected") == "venue_reject", "canonical cancel_rejected failed")) {
        return rc;
    }
    if (int rc = check(canonical_reason_code("missing_parameters") == "invalid_params", "canonical missing_parameters failed")) {
        return rc;
    }
    if (int rc = check(canonical_reason_code("exchange_error") == "network_error", "canonical exchange_error failed")) {
        return rc;
    }
    if (int rc = check(canonical_reason_code("risk_blocked") == "risk_blocked", "canonical risk_blocked failed")) {
        return rc;
    }
    if (int rc = check(canonical_reason_code("OK") == "ok", "canonical ok failed")) {
        return rc;
    }

    return EXIT_SUCCESS;
}
