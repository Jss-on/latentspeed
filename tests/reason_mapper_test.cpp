#include "core/reasons/reason_mapper.h"
#include <iostream>

using latentspeed::DefaultReasonMapper;

static int check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << std::endl;
        return 1;
    }
    return 0;
}

int main() {
    DefaultReasonMapper m;
    int rc = 0;
    rc += check(m.canonical_code("missing_parameters") == "invalid_params", "canonical missing_parameters");
    auto r = m.map("rejected", "balance insufficient");
    rc += check(r.reason_code == "insufficient_balance", "map rejected balance->insufficient_balance");
    if (rc == 0) std::cout << "reason_mapper_test: OK" << std::endl;
    return rc;
}

