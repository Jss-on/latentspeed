/**
 * @file credentials_resolver.h
 * @brief Central resolver for exchange credentials (CEX and DEX semantics).
 */

#pragma once

#include <string>

namespace latentspeed::auth {

struct ResolvedCredentials {
    std::string api_key;     // CEX: API key; DEX: wallet/user address (0x...)
    std::string api_secret;  // CEX: API secret; DEX: private key (hex)
    bool use_testnet{false};
};

// Parse common truthy/falsey strings from env.
bool parse_bool_env(const char* key, bool def_value);

// Resolve credentials for the given exchange using CLI values and environment.
ResolvedCredentials resolve_credentials(const std::string& exchange,
                                        const std::string& cli_api_key,
                                        const std::string& cli_api_secret,
                                        bool live_trade);

} // namespace latentspeed::auth

