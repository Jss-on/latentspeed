#include "core/symbol/symbol_mapper.h"
#include <iostream>

using latentspeed::DefaultSymbolMapper;

static int check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << std::endl;
        return 1;
    }
    return 0;
}

int main() {
    DefaultSymbolMapper m;
    int rc = 0;
    rc += check(m.to_compact("ETH/USDT:USDT", "perpetual") == "ETHUSDT", "to_compact ccxt+settle");
    rc += check(m.to_compact("ETH-USDT-PERP", "perpetual") == "ETHUSDT", "to_compact hyphen perp");
    rc += check(m.to_hyphen("ETHUSDT", false) == "ETH-USDT", "to_hyphen spot");
    rc += check(m.to_hyphen("ETHUSDT", true) == "ETH-USDT-PERP", "to_hyphen perp");
    if (rc == 0) std::cout << "symbol_mapper_test: OK" << std::endl;
    return rc;
}

