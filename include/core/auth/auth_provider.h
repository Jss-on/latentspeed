/**
 * @file auth_provider.h
 * @brief Phase 2: Auth provider interface and Bybit v5 HMAC implementation for pilot.
 */

#pragma once

#include <string>
#include <vector>
#include <utility>

namespace latentspeed::auth {

struct HeaderKV { std::string name; std::string value; };

class IAuthProvider {
public:
    virtual ~IAuthProvider() = default;
    // Build headers for REST request given inputs.
    virtual std::vector<HeaderKV> build_headers(const std::string& method,
                                                const std::string& endpoint,
                                                const std::string& params_json,
                                                const std::string& api_key,
                                                const std::string& api_secret,
                                                std::string& timestamp_out) const = 0;
};

class BybitAuthProvider final : public IAuthProvider {
public:
    std::vector<HeaderKV> build_headers(const std::string& method,
                                        const std::string& endpoint,
                                        const std::string& params_json,
                                        const std::string& api_key,
                                        const std::string& api_secret,
                                        std::string& timestamp_out) const override;
};

} // namespace latentspeed::auth

