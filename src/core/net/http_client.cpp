/**
 * @file http_client.cpp
 */

#include "core/net/http_client.h"
#include <curl/curl.h>
#include <stdexcept>
#include <sstream>

namespace latentspeed::nethttp {

static size_t write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    std::string* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), total);
    return total;
}

HttpClient::HttpClient() {}
HttpClient::~HttpClient() {}

std::string HttpClient::request(const std::string& method,
                                const std::string& scheme,
                                const std::string& host,
                                const std::string& port,
                                const std::string& target,
                                const std::vector<Header>& headers,
                                const std::string& body) const {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    std::string url = scheme + "://" + host;
    if (!port.empty() && port != "80" && port != "443") {
        url += ":" + port;
    }
    url += target; // target includes query string if provided

    std::string response;
    struct curl_slist* hdrs = nullptr;
    for (const auto& h : headers) {
        std::string line = h.name + ": " + h.value;
        hdrs = curl_slist_append(hdrs, line.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // allow compressed
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LatentSpeed/1.0");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    // Harden network behavior: ensure we never block indefinitely
    // - Avoid signals in multithreaded use
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    // - Aggressive but sane timeouts; configurable via env vars
    long connect_timeout_ms = 1500;   // default 1.5s
    long total_timeout_ms   = 2500;   // default 2.5s (IOC-friendly)
    if (const char* v = std::getenv("LATENTSPEED_HTTP_CONNECT_TIMEOUT_MS")) {
        char* endp = nullptr; long t = std::strtol(v, &endp, 10);
        if (endp && *endp == '\0' && t >= 100) connect_timeout_ms = t;
    }
    if (const char* v = std::getenv("LATENTSPEED_HTTP_TIMEOUT_MS")) {
        char* endp = nullptr; long t = std::strtol(v, &endp, 10);
        if (endp && *endp == '\0' && t >= 200) total_timeout_ms = t;
    }
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, total_timeout_ms);
    // - DNS cache to reduce resolver chatter (seconds)
    curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 60L);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
    } else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        std::ostringstream oss; oss << "curl error: " << curl_easy_strerror(rc);
        throw std::runtime_error(oss.str());
    }
    if (status >= 400) {
        std::ostringstream oss; oss << "HTTP status " << status << ": " << response;
        throw std::runtime_error(oss.str());
    }
    return response;
}

} // namespace latentspeed::nethttp
