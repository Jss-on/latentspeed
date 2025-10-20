/**
 * @file credentials_resolver.cpp
 */

#include "core/auth/credentials_resolver.h"
#include <cctype>
#include <cstdlib>

namespace latentspeed::auth {

namespace {
inline std::string to_upper_ascii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

inline std::string getenv_string(const std::string& key) {
    if (const char* v = std::getenv(key.c_str())) return std::string(v);
    return {};
}
} // namespace

bool parse_bool_env(const char* key, bool def_value) {
    const char* v = std::getenv(key);
    if (!v) return def_value;
    std::string s(v);
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
    if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    return def_value;
}

ResolvedCredentials resolve_credentials(const std::string& exchange,
                                        const std::string& cli_api_key,
                                        const std::string& cli_api_secret,
                                        bool live_trade) {
    ResolvedCredentials out{};
    const std::string exch_upper = to_upper_ascii(exchange);

    // Default testnet based on trading mode; allow env override.
    const std::string key_testnet = std::string("LATENTSPEED_") + exch_upper + "_USE_TESTNET";
    out.use_testnet = parse_bool_env(key_testnet.c_str(), !live_trade);

    // CLI takes precedence, else environment. DEX specializations map to the same fields.
    out.api_key = cli_api_key;
    out.api_secret = cli_api_secret;

    // If missing, resolve from env using standard names first.
    if (out.api_key.empty()) {
        out.api_key = getenv_string(std::string("LATENTSPEED_") + exch_upper + "_API_KEY");
    }
    if (out.api_secret.empty()) {
        out.api_secret = getenv_string(std::string("LATENTSPEED_") + exch_upper + "_API_SECRET");
    }

    // Hyperliquid (DEX) aliases: USER_ADDRESS and PRIVATE_KEY
    if (to_upper_ascii(exchange) == "HYPERLIQUID") {
        if (out.api_key.empty())    out.api_key    = getenv_string("LATENTSPEED_HYPERLIQUID_USER_ADDRESS");
        if (out.api_secret.empty()) out.api_secret = getenv_string("LATENTSPEED_HYPERLIQUID_PRIVATE_KEY");
    }

    return out;
}

} // namespace latentspeed::auth

