/**
 * @file bybit_auth_provider.cpp
 */

#include "core/auth/auth_provider.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace latentspeed::auth {

static std::string hmac_sha256_hex(const std::string& key, const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    HMAC(EVP_sha256(), key.c_str(), key.size(), reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), hash, &hash_len);
    std::ostringstream oss;
    for (unsigned i = 0; i < hash_len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

static std::string timestamp_ms() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(ms);
}

std::vector<HeaderKV> BybitAuthProvider::build_headers(const std::string& method,
                                                       const std::string& endpoint,
                                                       const std::string& params_json,
                                                       const std::string& api_key,
                                                       const std::string& api_secret,
                                                       std::string& timestamp_out) const {
    timestamp_out = timestamp_ms();
    const std::string recv_window = "5000";
    std::string sign_payload = timestamp_out + api_key + recv_window;
    if (method == "GET") {
        const auto qpos = endpoint.find('?');
        if (qpos != std::string::npos && qpos + 1 < endpoint.size()) {
            sign_payload += endpoint.substr(qpos + 1);
        }
    } else {
        if (!params_json.empty()) sign_payload += params_json;
    }
    const std::string signature = hmac_sha256_hex(api_secret, sign_payload);
    std::vector<HeaderKV> hdrs;
    hdrs.push_back({"X-BAPI-API-KEY", api_key});
    hdrs.push_back({"X-BAPI-TIMESTAMP", timestamp_out});
    hdrs.push_back({"X-BAPI-SIGN", signature});
    hdrs.push_back({"X-BAPI-RECV-WINDOW", recv_window});
    hdrs.push_back({"Content-Type", "application/json"});
    return hdrs;
}

} // namespace latentspeed::auth

