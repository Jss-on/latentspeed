/**
 * @file http_client.h
 * @brief Simple HTTP client wrapper using libcurl for REST requests (Phase 2 pilot).
 */

#pragma once

#include <string>
#include <vector>
#include <utility>

namespace latentspeed::nethttp {

struct Header { std::string name; std::string value; };

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // Perform an HTTP request. Throws std::runtime_error on failure.
    std::string request(const std::string& method,
                        const std::string& scheme,     // "https" or "http"
                        const std::string& host,
                        const std::string& port,       // e.g., "443"
                        const std::string& target,     // path + optional query
                        const std::vector<Header>& headers,
                        const std::string& body) const;
};

} // namespace latentspeed::nethttp
