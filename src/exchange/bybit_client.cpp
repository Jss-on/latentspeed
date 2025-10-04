/**
 * @file bybit_client.cpp
 * @brief Bybit exchange client implementation
 * @author jessiondiwangan@gmail.com
 * @date 2025
 * 
 * Direct Bybit API implementation replacing CCAPI dependency
 */

#include "exchange/bybit_client.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <algorithm>  // added for std::max

// Put near the top of bybit_client.cpp
namespace {
// Returns pointer to the "data" array if present and is an array; else nullptr.
inline const rapidjson::Value* get_data_array(const rapidjson::Document& doc) {
    if (doc.HasMember("data") && doc["data"].IsArray()) {
        return &doc["data"];
    }
    // Some payloads wrap the actual array inside { "data": { "result": [...] } }
    if (doc.HasMember("data") && doc["data"].IsObject()) {
        const auto& d = doc["data"];
        if (d.HasMember("result") && d["result"].IsArray()) {
            return &d["result"];
        }
    }
    return nullptr;
}
} // namespace

namespace {

// Exponential backoff with small jitter: 1s,2s,4s,8s,16s,30s cap, plus 0–250 ms jitter.
inline uint32_t next_backoff_ms(uint32_t& attempt) {
    const uint32_t base_ms = 250u;
    const uint32_t cap_ms  = 30000u;
    const uint32_t shift   = std::min<uint32_t>(attempt, 5u); // cap growth
    uint64_t delay = static_cast<uint64_t>(base_ms) << shift; // 1s << n
    if (delay > cap_ms) delay = cap_ms;
    // bump attempt for next time
    attempt = std::min<uint32_t>(attempt + 1u, 16u);

    // add jitter 0–250 ms
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 250);
    return static_cast<uint32_t>(delay) + static_cast<uint32_t>(dist(rng));
}

// Tiny guard to close socket if it's open
inline void safe_ws_close(std::unique_ptr<boost::beast::websocket::stream<
                              boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>>& ws) {
    if (!ws) return;
    boost::beast::error_code ec;
    ws->close(boost::beast::websocket::close_code::normal, ec);
}

} // namespace

namespace {

// Execution time cursor (ms since epoch) to avoid re-fetching old fills
static std::atomic<uint64_t> last_exec_time_cursor_ms{0};

inline void maybe_advance_exec_cursor(uint64_t exec_time_ms) {
    uint64_t prev = last_exec_time_cursor_ms.load(std::memory_order_relaxed);
    while (exec_time_ms > prev &&
           !last_exec_time_cursor_ms.compare_exchange_weak(prev, exec_time_ms,
                                                          std::memory_order_relaxed)) {
        /* CAS retry */
    }
}

} // namespace

namespace {
// return true if host is an IPv4/IPv6 literal (SNI should be skipped)
inline bool is_ip_literal(const std::string& host) {
    boost::system::error_code ec;
    (void)boost::asio::ip::make_address(host, ec);
    return !ec;
}
} // namespace

namespace {

// Known quote coins we strip to infer base coin from a symbol.
// Example: "BNBUSDT" -> base "BNB", "ETHBTC" -> base "ETH"
inline std::string extract_base_from_symbol(const std::string& sym) {
    static const std::array<const char*, 8> quotes = {
        "USDT","USDC","BTC","ETH","EUR","USD","DAI","FDUSD"
    };
    for (auto q : quotes) {
        const auto qlen = std::strlen(q);
        if (sym.size() > qlen && sym.rfind(q) == sym.size() - qlen) {
            return sym.substr(0, sym.size() - qlen);
        }
    }
    return {}; // unknown
}

// Pull a small set of "observed symbols" from pending orders (best hint for spot/option)
inline std::unordered_set<std::string> observed_symbols_from_pending(
    const std::map<std::string, latentspeed::OrderRequest>& pending) {
    std::unordered_set<std::string> out;
    out.reserve(pending.size());
    for (const auto& kv : pending) {
        if (!kv.second.symbol.empty()) out.insert(kv.second.symbol);
    }
    return out;
}

// Build a compact query plan that covers all categories without config.
// We:
//  - For linear: query settleCoin=USDT and settleCoin=USDC (covers most perps)
//  - For inverse: query a short baseCoin set (BTC, ETH) — safe & cheap
//  - For spot: if we have observed symbols, query them individually
//  - For option: if an observed symbol looks like an option (has '-' segments), query it
struct Query { std::string category; std::string qs; };

inline std::vector<Query> build_realtime_query_plan(
    const std::unordered_set<std::string>& observed_syms) {

    std::vector<Query> plan;
    plan.reserve(8 + observed_syms.size());

    // linear perps (USDT/USDC margined)
    plan.push_back({"linear", "category=linear&settleCoin=USDT"});
    plan.push_back({"linear", "category=linear&settleCoin=USDC"});

    // inverse perps (coin margined) — query a tiny allowlist of popular base coins
    for (const std::string base : {"BTC","ETH"}) {
        plan.push_back({"inverse", "category=inverse&baseCoin=" + base});
    }

    // spot & option — driven by observed symbols (if any)
    for (const auto& s : observed_syms) {
        // Heuristic: OPTION symbols on Bybit usually contain hyphens (e.g., "BTC-30SEP25-20000-C")
        const bool looks_option = (s.find('-') != std::string::npos);
        if (looks_option) {
            plan.push_back({"option", "category=option&symbol=" + s});
            continue;
        }
        // Otherwise treat as spot symbol (Bybit uses the same "symbol" token, e.g., "BNBUSDT")
        plan.push_back({"spot", "category=spot&symbol=" + s});
    }

    return plan;
}

} // namespace


namespace latentspeed {

void BybitClient::RateLimiter::throttle() {
    using clock = std::chrono::steady_clock;
    std::unique_lock<std::mutex> lock(mutex);

    const auto now = clock::now();
    while (!history.empty() && (now - history.front()) >= window) {
        history.pop_front();
    }

    if (history.size() >= max_per_window) {
        const auto wait_duration = window - (now - history.front());
        if (wait_duration.count() > 0) {
            lock.unlock();
            std::this_thread::sleep_for(wait_duration);
            lock.lock();
        }
        const auto refreshed = clock::now();
        while (!history.empty() && (refreshed - history.front()) >= window) {
            history.pop_front();
        }
    }

    history.push_back(clock::now());
}

BybitClient::BybitClient() 
    : ioc_(std::make_unique<net::io_context>()),
      ssl_ctx_(std::make_unique<ssl::context>(ssl::context::tlsv12_client)),
      rest_ioc_(std::make_unique<net::io_context>()),
      rest_resolver_(std::make_unique<tcp::resolver>(*rest_ioc_)) {
    
    // Configure SSL context
    ssl_ctx_->set_default_verify_paths();
    ssl_ctx_->set_verify_mode(ssl::verify_peer);
}

BybitClient::~BybitClient() {
    try {
        should_stop_.store(true);
        ws_connected_.store(false);
        ws_send_cv_.notify_all();

        if (ws_) {
            boost::beast::error_code ec;
            ws_->close(boost::beast::websocket::close_code::normal, ec);
            boost::beast::get_lowest_layer(*ws_).close(ec);
        }
        if (ws_thread_ && ws_thread_->joinable()) ws_thread_->join();

        {   // drain send queue
            std::lock_guard<std::mutex> qlk(ws_send_mutex_);
            std::queue<std::string> empty;
            std::swap(ws_send_queue_, empty);
        }

        {   // close REST
            std::scoped_lock lk(rest_mutex_);
            close_rest_connection_locked();
        }
    } catch (...) {}
}

bool BybitClient::initialize(const std::string& api_key, 
                             const std::string& api_secret,
                             bool testnet) {
    try {
        api_key_ = api_key;
        api_secret_ = api_secret;
        is_testnet_ = testnet;

        // PR1: centralized endpoint selection (no behavior change)
        configure_endpoints(testnet);
        demo_mode_ = (rest_host_ == std::string("api-demo.bybit.com"));
        spdlog::info("[BybitClient] Endpoint matrix: demo_mode={}, REST={}, WS={}",
                     demo_mode_, rest_host_, ws_host_);

        rest_stream_ = std::make_unique<ssl::stream<tcp::socket>>(*rest_ioc_, *ssl_ctx_);
        rest_connected_ = false;
        rest_endpoints_.clear();
        try {
            auto results = rest_resolver_->resolve(rest_host_, rest_port_);
            for (const auto& entry : results) {
                rest_endpoints_.push_back(entry.endpoint());
            }
        } catch (const std::exception& e) {
            spdlog::warn("[BybitClient] REST resolve failed during initialize: {}", e.what());
        }

        spdlog::info("[BybitClient] Initialized for {} environment", 
                     testnet ? "testnet" : "production");
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] Initialization failed: {}", e.what());
        return false;
    }
}

bool BybitClient::connect() {
    try {
        // Mark as connected for REST API (stateless)
        connected_ = true;
        
        // Start WebSocket thread for real-time updates
        should_stop_ = false;
        ws_thread_ = std::make_unique<std::thread>(&BybitClient::websocket_thread_func, this);
        
        // Wait briefly for WebSocket connection
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        if (ws_connected_) {
            spdlog::info("[BybitClient] Successfully connected to Bybit");
            return true;
        } else {
            spdlog::warn("[BybitClient] REST connected but WebSocket connection pending");
            return true;  // REST API still works
        }
        
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] Connection failed: {}", e.what());
        connected_ = false;
        return false;
    }
}

void BybitClient::disconnect() {
    should_stop_ = true;
    connected_ = false;
    ws_connected_ = false;

    // Stop WebSocket thread
    if (ws_thread_ && ws_thread_->joinable()) {
        ws_send_cv_.notify_all();
        ws_thread_->join();
    }

    {
        std::lock_guard<std::mutex> lock(rest_mutex_);
        close_rest_connection_locked();
    }

    spdlog::info("[BybitClient] Disconnected from Bybit");
}

bool BybitClient::is_connected() const {
    return connected_.load();
}

OrderResponse BybitClient::place_order(const OrderRequest& request) {
    try {
        // Build order parameters
        rapidjson::Document params;
        params.SetObject();
        auto& allocator = params.GetAllocator();
        
        // Required fields
        params.AddMember("category", 
                        rapidjson::Value(request.category.value_or("spot").c_str(), allocator),
                        allocator);
        params.AddMember("symbol", rapidjson::Value(request.symbol.c_str(), allocator), allocator);
        params.AddMember("side", 
                        rapidjson::Value(request.side == "buy" ? "Buy" : "Sell", allocator),
                        allocator);
        params.AddMember("orderType", 
                        rapidjson::Value(request.order_type == "limit" ? "Limit" : "Market", allocator),
                        allocator);
        params.AddMember("qty", rapidjson::Value(request.quantity.c_str(), allocator), allocator);
        
        // Optional fields
        if (request.price.has_value() && request.order_type == "limit") {
            params.AddMember("price", rapidjson::Value(request.price.value().c_str(), allocator), allocator);
        }
        
        if (request.time_in_force.has_value()) {
            params.AddMember("timeInForce", rapidjson::Value(request.time_in_force.value().c_str(), allocator), allocator);
        }

        // CRITICAL: Handle reduce_only for position management
        // Always include reduceOnly parameter for derivatives (linear/inverse)
        std::string category = request.category.value_or("spot");
        if (category != "spot") {
            // Always add the reduceOnly parameter for derivatives
            params.AddMember("reduceOnly", rapidjson::Value(request.reduce_only), allocator);
            
            // For derivatives, also add positionIdx (0 for one-way mode, 1 for buy side, 2 for sell side in hedge mode)
            // Default to 0 (one-way mode) - this is the most common setup
            params.AddMember("positionIdx", rapidjson::Value(0), allocator);
            
            if (request.reduce_only) {
                spdlog::info("[BybitClient] ✅ REDUCE-ONLY order for {} ({}): {} - category: {}, positionIdx: 0", 
                           request.symbol, category, request.client_order_id, category);
            } else {
                spdlog::debug("[BybitClient] Regular order (not reduce-only): {} - category: {}, positionIdx: 0", 
                             request.client_order_id, category);
            }
        } else if (request.reduce_only) {
            // Warn if trying to use reduce-only on spot
            spdlog::warn("[BybitClient] ❌ reduce_only ignored for spot order: {} - category: {}", 
                        request.client_order_id, category);
        }
        
        params.AddMember("orderLinkId", rapidjson::Value(request.client_order_id.c_str(), allocator), allocator);

        for (const auto& [key, value] : request.extra_params) {
            if (params.HasMember(key.c_str())) {
                params[key.c_str()].SetString(value.c_str(), allocator);
            } else {
                rapidjson::Value json_key(key.c_str(), allocator);
                rapidjson::Value json_val(value.c_str(), allocator);
                params.AddMember(json_key, json_val, allocator);
            }
        }
        
        // Convert to JSON string
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        params.Accept(writer);
        std::string params_json = buffer.GetString();
        
        // LOG THE ACTUAL JSON PAYLOAD BEING SENT
        spdlog::info("[BybitClient] 📨 Sending to Bybit API: {}", params_json);
        
        // Store pending order
        {
            std::lock_guard<std::mutex> lock(orders_mutex_);
            pending_orders_[request.client_order_id] = request;
        }
        
        // Make REST request
        std::string response_json = make_rest_request("POST", "/v5/order/create", params_json);
        
        OrderResponse response = parse_order_response(response_json);
        if (response.success && response.exchange_order_id.has_value()) {
            std::lock_guard<std::mutex> lock(orders_mutex_);
            auto it = pending_orders_.find(request.client_order_id);
            if (it != pending_orders_.end()) {
                it->second.extra_params["exchange_order_id"] = response.exchange_order_id.value();
            }
        }
        
        return response;
        
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] Failed to place order: {}", e.what());
        return OrderResponse{false, e.what(), std::nullopt, std::nullopt, std::nullopt, {}};
    }
}

OrderResponse BybitClient::cancel_order(const std::string& client_order_id,
                                       const std::optional<std::string>& symbol,
                                       const std::optional<std::string>& exchange_order_id) {
    try {
        // Build cancel parameters
        rapidjson::Document params;
        params.SetObject();
        auto& allocator = params.GetAllocator();
        
        // Try to get order info from pending orders
        std::string category = "spot";
        std::string order_symbol = symbol.value_or("");
        
        {
            std::lock_guard<std::mutex> lock(orders_mutex_);
            auto it = pending_orders_.find(client_order_id);
            if (it != pending_orders_.end()) {
                category = it->second.category.value_or("spot");
                if (order_symbol.empty()) {
                    order_symbol = it->second.symbol;
                }
            }
        }
        
        params.AddMember("category", rapidjson::Value(category.c_str(), allocator), allocator);
        params.AddMember("orderLinkId", rapidjson::Value(client_order_id.c_str(), allocator), allocator);
        
        if (!order_symbol.empty()) {
            params.AddMember("symbol", rapidjson::Value(order_symbol.c_str(), allocator), allocator);
        }

        if (exchange_order_id.has_value() && !exchange_order_id->empty()) {
            params.AddMember("orderId", rapidjson::Value(exchange_order_id->c_str(), allocator), allocator);
        }
        
        // Convert to JSON string
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        params.Accept(writer);
        std::string params_json = buffer.GetString();
        
        // Make REST request
        std::string response_json = make_rest_request("POST", "/v5/order/cancel", params_json);
        OrderResponse response = parse_order_response(response_json);
        if (response.success) {
            std::lock_guard<std::mutex> lock(orders_mutex_);
            pending_orders_.erase(client_order_id);
        }
        return response;
        
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] Failed to cancel order: {}", e.what());
        return OrderResponse{false, e.what(), std::nullopt, std::nullopt, std::nullopt, {}};
    }
}

OrderResponse BybitClient::modify_order(const std::string& client_order_id,
                                       const std::optional<std::string>& new_quantity,
                                       const std::optional<std::string>& new_price) {
    try {
        // Build amend parameters
        rapidjson::Document params;
        params.SetObject();
        auto& allocator = params.GetAllocator();
        
        // Get order info
        std::string category = "spot";
        std::string symbol;
        
        {
            std::lock_guard<std::mutex> lock(orders_mutex_);
            auto it = pending_orders_.find(client_order_id);
            if (it != pending_orders_.end()) {
                category = it->second.category.value_or("spot");
                symbol = it->second.symbol;
            }
        }
        
        params.AddMember("category", rapidjson::Value(category.c_str(), allocator), allocator);
        params.AddMember("orderLinkId", rapidjson::Value(client_order_id.c_str(), allocator), allocator);
        params.AddMember("symbol", rapidjson::Value(symbol.c_str(), allocator), allocator);
        
        if (new_quantity.has_value()) {
            params.AddMember("qty", rapidjson::Value(new_quantity.value().c_str(), allocator), allocator);
        }
        
        if (new_price.has_value()) {
            params.AddMember("price", rapidjson::Value(new_price.value().c_str(), allocator), allocator);
        }
        
        // Convert to JSON string
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        params.Accept(writer);
        std::string params_json = buffer.GetString();
        
        // Make REST request
        std::string response = make_rest_request("POST", "/v5/order/amend", params_json);
        
        // Parse response
        return parse_order_response(response);
        
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] Failed to modify order: {}", e.what());
        return OrderResponse{false, e.what(), std::nullopt, std::nullopt, std::nullopt, {}};
    }
}

OrderResponse BybitClient::query_order(const std::string& client_order_id) {
    try {
        // Build query parameters
        rapidjson::Document params;
        params.SetObject();
        auto& allocator = params.GetAllocator();
        (void)allocator; // Mark as used to avoid warning
        
        // Get order info
        std::string category = "spot";
        
        {
            std::lock_guard<std::mutex> lock(orders_mutex_);
            auto it = pending_orders_.find(client_order_id);
            if (it != pending_orders_.end()) {
                category = it->second.category.value_or("spot");
            }
        }
        
        // Build query string
        std::string query = "category=" + category + "&orderLinkId=" + client_order_id;
        
        // Make REST request (GET request)
        std::string response = make_rest_request("GET", "/v5/order/realtime?" + query);
        
        // Parse response
        return parse_order_response(response);
        
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] Failed to query order: {}", e.what());
        return OrderResponse{false, e.what(), std::nullopt, std::nullopt, std::nullopt, {}};
    }
}

void BybitClient::set_order_update_callback(OrderUpdateCallback callback) {
    order_update_callback_ = callback;
}

void BybitClient::set_fill_callback(FillCallback callback) {
    fill_callback_ = callback;
}

void BybitClient::set_error_callback(ErrorCallback callback) {
    error_callback_ = callback;
}

bool BybitClient::subscribe_to_orders(const std::vector<std::string>& symbols) {
    if (!ws_connected_) {
        spdlog::warn("[BybitClient] WebSocket not connected, cannot subscribe");
        return false;
    }
    
    // Bybit v5 subscribes to order updates for all symbols automatically
    // once authenticated on the private channel
    std::vector<std::string> topics = {"order", "execution"};
    return send_websocket_subscribe(topics);
}

// Private methods implementation

std::string BybitClient::make_rest_request(const std::string& method,
                                          const std::string& endpoint,
                                          const std::string& params_json) {
    try {
        std::lock_guard<std::mutex> lock(rest_mutex_);
        rest_rate_limiter_.throttle();
        return perform_rest_request_locked(method, endpoint, params_json);
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] REST request failed: {}", e.what());
        throw;
    }
}

bool BybitClient::ensure_rest_connection_locked() {
    // Fast path: still connected and socket open
    if (rest_connected_ && rest_stream_ && rest_stream_->lowest_layer().is_open()) {
        return true;
    }

    // Tear down any old stream and create a fresh TLS stream
    close_rest_connection_locked();
    rest_stream_ = std::make_unique<ssl::stream<tcp::socket>>(*rest_ioc_, *ssl_ctx_);

    // ------ SNI (skip for IPs) ------
    if (!is_ip_literal(rest_host_)) {
        if (!SSL_set_tlsext_host_name(rest_stream_->native_handle(), rest_host_.c_str())) {
            spdlog::error("[BybitClient] Failed to set SNI (REST) for host {}", rest_host_);
            return false;
        }
        // Optional but recommended: enable hostname verification against cert
        X509_VERIFY_PARAM* param = SSL_get0_param(rest_stream_->native_handle());
        // (flags are optional; pass 0 if you prefer to allow wildcards)
        X509_VERIFY_PARAM_set_hostflags(param, 0);
        if (X509_VERIFY_PARAM_set1_host(param, rest_host_.c_str(), 0) != 1) {
            spdlog::warn("[BybitClient] Failed to set hostname verification for {}", rest_host_);
        }
    }

    // Resolve endpoints once (cache)
    if (rest_endpoints_.empty()) {
        try {
            auto results = rest_resolver_->resolve(rest_host_, rest_port_);
            std::vector<tcp::endpoint> v4, v6;
             for (const auto& r : results) {
                 const auto ep = r.endpoint();
                 (ep.address().is_v4() ? v4 : v6).push_back(ep);
             }
             rest_endpoints_.clear();
             rest_endpoints_.insert(rest_endpoints_.end(), v4.begin(), v4.end());
             rest_endpoints_.insert(rest_endpoints_.end(), v6.begin(), v6.end());
        } catch (const std::exception& e) {
            spdlog::error("[BybitClient] REST resolve failed: {}", e.what());
            return false;
        }
    }

    // Try endpoints until one succeeds
    boost::beast::error_code ec;
    for (const auto& ep : rest_endpoints_) {
        rest_stream_->lowest_layer().connect(ep, ec);
        if (ec) {
            spdlog::warn("[BybitClient] REST TCP connect to {}:{} failed: {}",
                        ep.address().to_string(), ep.port(), ec.message());
            // ensure the underlying FD is not left around
            rest_stream_->lowest_layer().close(ec);
            // recreate a fresh TLS stream for the next attempt
            rest_stream_ = std::make_unique<ssl::stream<tcp::socket>>(*rest_ioc_, *ssl_ctx_);
            if (!is_ip_literal(rest_host_)) {
                (void)SSL_set_tlsext_host_name(rest_stream_->native_handle(), rest_host_.c_str());
                X509_VERIFY_PARAM* param = SSL_get0_param(rest_stream_->native_handle());
                X509_VERIFY_PARAM_set_hostflags(param, 0);
                (void)X509_VERIFY_PARAM_set1_host(param, rest_host_.c_str(), 0);
            }
            continue;
        }

        // TCP options...
        rest_stream_->lowest_layer().set_option(boost::asio::socket_base::keep_alive(true), ec);
        rest_stream_->lowest_layer().set_option(boost::asio::ip::tcp::no_delay(true), ec);

        // TLS handshake ...
        rest_stream_->handshake(ssl::stream_base::client, ec);
        if (ec) {
            spdlog::warn("[BybitClient] REST TLS handshake failed: {}", ec.message());
            rest_stream_->lowest_layer().close(ec);
            rest_stream_ = std::make_unique<ssl::stream<tcp::socket>>(*rest_ioc_, *ssl_ctx_);
            if (!is_ip_literal(rest_host_)) {
                (void)SSL_set_tlsext_host_name(rest_stream_->native_handle(), rest_host_.c_str());
                X509_VERIFY_PARAM* param = SSL_get0_param(rest_stream_->native_handle());
                X509_VERIFY_PARAM_set_hostflags(param, 0);
                (void)X509_VERIFY_PARAM_set1_host(param, rest_host_.c_str(), 0);
            }
            continue;
        }

        rest_connected_ = true;
        return true;
    }

    rest_connected_ = false;
    return false;
}


void BybitClient::configure_endpoints(bool testnet) {
    if (testnet) {
        // Bybit mainnet demo trading (paper) endpoints
        rest_host_ = "api-demo.bybit.com";
        rest_port_ = "443";
        ws_host_ = "stream-demo.bybit.com";
        ws_port_ = "443";
        ws_target_ = "/v5/private";
    } else {
        // Bybit production endpoints
        rest_host_ = "api.bybit.com";
        rest_port_ = "443";
        ws_host_ = "stream.bybit.com";
        ws_port_ = "443";
        ws_target_ = "/v5/private";
    }
}

bool BybitClient::build_ws_auth_payload(std::string& out_json) {
    try {
        // Bybit sample: signature = HMAC_SHA256(secret, "GET/realtime" + expires)
        // Keep expires tight (now + 1000ms).
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto expires = now + 1000;

        std::string sign_src = std::string("GET/realtime") + std::to_string(expires);
        const std::string sig = hmac_sha256(api_secret_, sign_src);

        // {"op":"auth","args":[apiKey, expiresMs, signature]}
        out_json.reserve(96 + api_key_.size() + sig.size());
        out_json = std::string("{\"op\":\"auth\",\"args\":[\"")
                 + api_key_ + "\",\"" + std::to_string(expires) + "\",\"" + sig + "\"]}";
        return true;
    } catch (...) {
        return false;
    }
}

void BybitClient::close_rest_connection_locked() {
    if (!rest_stream_) return;
    boost::beast::error_code ec;
    // Best-effort cleanup; ignore errors
    rest_stream_->shutdown(ec);
    rest_stream_->lowest_layer().shutdown(tcp::socket::shutdown_both, ec);
    rest_stream_->lowest_layer().close(ec);
    rest_connected_ = false;
    rest_stream_.reset();              // <-- IMPORTANT: drop the shutdown SSL object
}

void BybitClient::resync_pending_orders() {
    // Snapshot open orders across categories, then exec catch-up.
    // We drive spot/option queries from any symbols we've seen in pending_orders_.

    // ---- Build a symbol hint set from pending orders (thread-safe copy) ----
    std::unordered_set<std::string> observed_syms;
    {
        std::lock_guard<std::mutex> lk(orders_mutex_);
        observed_syms = observed_symbols_from_pending(pending_orders_);
    }

    // ---- 1) Open orders snapshot ----
    const auto plan = build_realtime_query_plan(observed_syms);

    size_t total_orders = 0;
    for (const auto& q : plan) {
        const std::string endpoint = "/v5/order/realtime?" + q.qs; // default returns OPEN orders

        std::string body;
        try {
            body = perform_rest_request_locked("GET", endpoint, "");
        } catch (const std::exception& e) {
            spdlog::warn("[BybitClient] resync {}: GET {} failed: {}", q.category, endpoint, e.what());
            continue;
        }

        rapidjson::Document doc;
        doc.Parse(body.c_str());
        if (doc.HasParseError() || !doc.IsObject()) {
            spdlog::warn("[BybitClient] resync {}: invalid JSON", q.category);
            continue;
        }

        const int ret = doc.HasMember("retCode") && doc["retCode"].IsInt() ? doc["retCode"].GetInt() : -1;
        if (ret != 0) {
            const char* msg = (doc.HasMember("retMsg") && doc["retMsg"].IsString()) ? doc["retMsg"].GetString() : "";
            spdlog::warn("[BybitClient] resync {}: retCode={} retMsg='{}' (qs: {})", q.category, ret, msg, q.qs);
            continue;
        }

        // result.list || result.orders || data
        const rapidjson::Value* list = nullptr;
        if (doc.HasMember("result") && doc["result"].IsObject()) {
            const auto& res = doc["result"];
            if (res.HasMember("list")   && res["list"].IsArray())   list = &res["list"];
            else if (res.HasMember("orders") && res["orders"].IsArray()) list = &res["orders"];
        } else if (doc.HasMember("data") && doc["data"].IsArray()) {
            list = &doc["data"];
        }
        if (!list) {
            spdlog::info("[BybitClient] resync {}: empty list (qs: {})", q.category, q.qs);
            continue;
        }

        for (auto it = list->Begin(); it != list->End(); ++it) {
            if (it->IsObject()) emit_order_snapshot(*it);
        }
        total_orders += list->Size();
    }

    spdlog::info("[BybitClient] resync: applied {} open orders across categories", total_orders);

    // ---- 2) Executions catch-up (cursor-based), across the same categories ----
    const uint64_t start_ms = last_exec_time_cursor_ms.load(std::memory_order_relaxed);
    if (start_ms == 0) {
        spdlog::info("[BybitClient] resync: no execution cursor yet; skipping exec backfill");
        return;
    }

    size_t total_execs = 0;
    uint64_t newest_ts = start_ms;

    // Reuse categories from the plan but dedupe categories to avoid duplicate calls
    std::unordered_set<std::string> cats;
    cats.reserve(plan.size());
    for (const auto& q : plan) cats.insert(q.category);

    for (const auto& cat : cats) {
        const std::string ep = "/v5/execution/list?category=" + cat +
                               "&startTime=" + std::to_string(start_ms) +
                               "&limit=200";

        std::string body;
        try {
            body = perform_rest_request_locked("GET", ep, "");
        } catch (const std::exception& e) {
            spdlog::warn("[BybitClient] resync {}: execution/list failed: {}", cat, e.what());
            continue;
        }

        rapidjson::Document doc;
        doc.Parse(body.c_str());
        if (doc.HasParseError() || !doc.IsObject()) {
            spdlog::warn("[BybitClient] resync {}: invalid JSON from execution/list", cat);
            continue;
        }

        const int ret = doc.HasMember("retCode") && doc["retCode"].IsInt() ? doc["retCode"].GetInt() : -1;
        if (ret != 0) {
            const char* msg = (doc.HasMember("retMsg") && doc["retMsg"].IsString()) ? doc["retMsg"].GetString() : "";
            spdlog::warn("[BybitClient] resync {}: execution retCode={} retMsg='{}'", cat, ret, msg);
            continue;
        }

        const rapidjson::Value* list = nullptr;
        if (doc.HasMember("result") && doc["result"].IsObject()) {
            const auto& res = doc["result"];
            if (res.HasMember("list") && res["list"].IsArray()) list = &res["list"];
        } else if (doc.HasMember("data") && doc["data"].IsArray()) {
            list = &doc["data"];
        }
        if (!list) continue;

        for (auto it = list->Begin(); it != list->End(); ++it) {
            if (!it->IsObject()) continue;
            emit_execution_snapshot(*it);

            // track newest timestamp to advance cursor
            if (it->HasMember("execTime")) {
                if ((*it)["execTime"].IsUint64()) {
                    newest_ts = std::max<uint64_t>(newest_ts, (*it)["execTime"].GetUint64());
                } else if ((*it)["execTime"].IsString()) {
                    try { newest_ts = std::max<uint64_t>(newest_ts, std::stoull((*it)["execTime"].GetString())); } catch (...) {}
                }
            } else if (it->HasMember("time") && (*it)["time"].IsUint64()) {
                newest_ts = std::max<uint64_t>(newest_ts, (*it)["time"].GetUint64());
            }
            ++total_execs;
        }
    }

    if (total_execs > 0) {
        maybe_advance_exec_cursor(newest_ts);
    }
    spdlog::info("[BybitClient] resync: applied {} executions (start >= {})", total_execs, start_ms);
}




void BybitClient::resync_order(const std::string& client_order_id, const OrderRequest& snapshot) {
    const std::string category = snapshot.category.value_or("spot");

    std::string order_response = make_rest_request(
        "GET",
        "/v5/order/realtime?category=" + category + "&orderLinkId=" + client_order_id);

    rapidjson::Document doc;
    doc.Parse(order_response.c_str());
    if (doc.HasParseError() || !doc.HasMember("retCode") || doc["retCode"].GetInt() != 0) {
        return;
    }

    if (!doc.HasMember("result")) {
        return;
    }
    const auto& result = doc["result"];
    if (!result.HasMember("list") || !result["list"].IsArray()) {
        return;
    }

    std::string exchange_order_id;
    for (const auto& order_entry : result["list"].GetArray()) {
        emit_order_snapshot(order_entry);
        if (order_entry.HasMember("orderId")) {
            exchange_order_id = order_entry["orderId"].GetString();
            std::lock_guard<std::mutex> lock(orders_mutex_);
            auto it = pending_orders_.find(client_order_id);
            if (it != pending_orders_.end()) {
                it->second.extra_params["exchange_order_id"] = exchange_order_id;
            }
        }
    }

    if (exchange_order_id.empty()) {
        return;
    }

    std::string exec_response = make_rest_request(
        "GET",
        "/v5/execution/list?category=" + category + "&orderId=" + exchange_order_id + "&limit=50");

    rapidjson::Document exec_doc;
    exec_doc.Parse(exec_response.c_str());
    if (exec_doc.HasParseError() || !exec_doc.HasMember("retCode") || exec_doc["retCode"].GetInt() != 0) {
        return;
    }

    if (!exec_doc.HasMember("result")) {
        return;
    }
    const auto& exec_result = exec_doc["result"];
    if (!exec_result.HasMember("list") || !exec_result["list"].IsArray()) {
        return;
    }

    for (const auto& exec_entry : exec_result["list"].GetArray()) {
        emit_execution_snapshot(exec_entry);
    }
}

void BybitClient::emit_order_snapshot(const rapidjson::Value& order_data) {
    if (!order_data.HasMember("orderLinkId")) {
        return;
    }

    OrderUpdate update;
    update.client_order_id = order_data["orderLinkId"].GetString();
    if (order_data.HasMember("orderId")) {
        update.exchange_order_id = order_data["orderId"].GetString();
    }
    if (order_data.HasMember("orderStatus")) {
        update.status = order_data["orderStatus"].GetString();
    }
    if (order_data.HasMember("cancelReason")) {
        update.reason = order_data["cancelReason"].GetString();
    } else if (order_data.HasMember("rejectReason")) {
        update.reason = order_data["rejectReason"].GetString();
    }
    if (order_data.HasMember("updatedTime")) {
        update.timestamp_ms = std::stoull(order_data["updatedTime"].GetString());
    }

    if (order_update_callback_) {
        order_update_callback_(update);
    }
}

http::request<http::string_body>
BybitClient::build_signed_request(const std::string& method,
                                  const std::string& endpoint,
                                  const std::string& params_json,
                                  std::string& timestamp_out) {
    http::request<http::string_body> req;
    req.version(11);
    req.method(method == "GET" ? http::verb::get : http::verb::post);
    req.target(endpoint);                    // keep the query in the target as-is
    req.set(http::field::host, rest_host_);
    req.set(http::field::user_agent, "LatentSpeed/1.0");
    req.set(http::field::content_type, "application/json");
    req.set(http::field::connection, "keep-alive");

    timestamp_out = get_timestamp_ms();
    std::string recv_window = "5000";

    // ---- build sign payload per Bybit v5 ----
    std::string sign_payload = timestamp_out + api_key_ + recv_window;

    if (req.method() == http::verb::get) {
        // append queryString (no leading '?') if present
        const auto qpos = endpoint.find('?');
        if (qpos != std::string::npos && qpos + 1 < endpoint.size()) {
            const std::string query_string = endpoint.substr(qpos + 1);
            sign_payload += query_string;
        }
        // no body for GET
    } else {
        // POST: append raw JSON body (must exactly match what you send)
        if (!params_json.empty()) {
            sign_payload += params_json;
            req.body() = params_json;
        }
    }
    // -----------------------------------------

    const std::string signature = hmac_sha256(api_secret_, sign_payload);
    req.set("X-BAPI-API-KEY", api_key_);
    req.set("X-BAPI-TIMESTAMP", timestamp_out);
    req.set("X-BAPI-SIGN", signature);
    req.set("X-BAPI-RECV-WINDOW", recv_window);
    // req.set("X-BAPI-SIGN-TYPE", "2"); // optional (HMAC-SHA256), default is fine
    req.prepare_payload();
    return req;
}


std::string BybitClient::perform_rest_request_locked(const std::string& method,
                                                     const std::string& endpoint,
                                                     const std::string& params_json) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!ensure_rest_connection_locked()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        try {
            std::string timestamp;
            auto req = build_signed_request(method, endpoint, params_json, timestamp);
            
            http::write(*rest_stream_, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(*rest_stream_, buffer, res);

            if (res.need_eof()) {
                close_rest_connection_locked();
            }
            bool server_wants_close = false;
            auto conn_hdr = res.base().find(http::field::connection);
            if (conn_hdr != res.base().end()) {
                // case-insensitive compare is fine; Beast normalizes flags internally too
                std::string v(conn_hdr->value().data(), conn_hdr->value().size());
                std::transform(v.begin(), v.end(), v.begin(), ::tolower);
                server_wants_close = (v.find("close") != std::string::npos);
            }

            if (server_wants_close || !res.keep_alive()) {
                // server intends to close; match it to avoid lingering half-open FDs
                close_rest_connection_locked();
            }

            return res.body();

        } catch (const std::exception& e) {
            spdlog::warn("[BybitClient] REST request attempt {} failed: {}", attempt + 1, e.what());
            close_rest_connection_locked();
        }
    }

    throw std::runtime_error("REST connection failure");
}

const std::vector<tcp::endpoint>& BybitClient::resolve_ws_endpoints(bool force_refresh) {
    auto now = std::chrono::steady_clock::now();

    const bool cache_fresh =
        !ws_endpoints_cache_.empty() &&
        (now - ws_dns_last_ok_) < ws_dns_ttl_;

    if (cache_fresh && !force_refresh) {
        return ws_endpoints_cache_;
    }

    try {
        tcp::resolver resolver(*ioc_);
        auto results = resolver.resolve(ws_host_, ws_port_);

        std::vector<tcp::endpoint> v4, v6;
        v4.reserve(8);
        v6.reserve(8);
        for (const auto& r : results) {
            const auto ep = r.endpoint();
            (ep.address().is_v4() ? v4 : v6).push_back(ep);
        }

        ws_endpoints_cache_.clear();
        if (prefer_ipv4_) {
            ws_endpoints_cache_.insert(ws_endpoints_cache_.end(), v4.begin(), v4.end());
            ws_endpoints_cache_.insert(ws_endpoints_cache_.end(), v6.begin(), v6.end());
        } else {
            ws_endpoints_cache_.insert(ws_endpoints_cache_.end(), v6.begin(), v6.end());
            ws_endpoints_cache_.insert(ws_endpoints_cache_.end(), v4.begin(), v4.end());
        }

        ws_dns_last_ok_ = now;
        return ws_endpoints_cache_;

    } catch (const std::exception& e) {
        if (!ws_endpoints_cache_.empty()) {
            spdlog::warn(
                "[BybitClient] DNS resolve failed ({}); reusing cached WS endpoints ({} addrs, age={}s)",
                e.what(),
                ws_endpoints_cache_.size(),
                std::chrono::duration_cast<std::chrono::seconds>(now - ws_dns_last_ok_).count()
            );
            return ws_endpoints_cache_;
        }
        spdlog::error("[BybitClient] DNS resolve failed and no cached WS endpoints are available: {}", e.what());
        throw; // caller will back off & retry
    }
}


std::string BybitClient::hmac_sha256(const std::string& key, const std::string& data) const {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    
    HMAC(EVP_sha256(), 
         key.c_str(), key.length(),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         hash, &hash_len);
    
    // Convert to hex string
    std::stringstream ss;
    for (unsigned int i = 0; i < hash_len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    
    return ss.str();
}

std::string BybitClient::get_timestamp_ms() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return std::to_string(ms.count());
}

OrderResponse BybitClient::parse_order_response(const std::string& json_response) {
    try {
        rapidjson::Document doc;
        doc.Parse(json_response.c_str());
        
        if (doc.HasParseError()) {
            return OrderResponse{false, "Failed to parse JSON response", std::nullopt, std::nullopt, std::nullopt, {}};
        }
        
        OrderResponse response;
        response.extra_data["raw"] = json_response;
        
        if (doc.HasMember("retCode") && doc["retCode"].GetInt() == 0) {
            response.success = true;
            response.message = doc.HasMember("retMsg") ? doc["retMsg"].GetString() : "Success";
            
        if (doc.HasMember("result")) {
            const auto& res = doc["result"];
            // bybit v5 usually nests in result.list[0]
            if (res.HasMember("list") && res["list"].IsArray() && !res["list"].Empty()) {
                const auto& o = res["list"][0];
                if (o.HasMember("symbol") && o["symbol"].IsString())
                    response.extra_data["symbol"] = o["symbol"].GetString();
                if (o.HasMember("category") && o["category"].IsString())
                    response.extra_data["category"] = o["category"].GetString();
            } else {
                if (res.HasMember("symbol") && res["symbol"].IsString())
                    response.extra_data["symbol"] = res["symbol"].GetString();
                if (res.HasMember("category") && res["category"].IsString())
                    response.extra_data["category"] = res["category"].GetString();
            }
        }
        } else {
            response.success = false;
            response.message = doc.HasMember("retMsg") ? doc["retMsg"].GetString() : "Unknown error";
            if (doc.HasMember("retCode") && doc["retCode"].IsInt()) {
                response.extra_data["retCode"] = std::to_string(doc["retCode"].GetInt());
            }
        }
        
        return response;
        
    } catch (const std::exception& e) {
        return OrderResponse{false, std::string("Parse error: ") + e.what(), std::nullopt, std::nullopt, std::nullopt, {}};
    }
}

std::string BybitClient::map_order_status(const std::string& bybit_status) const {
    std::string lower = bybit_status;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lower == "new" || lower == "partiallyfilled" || lower == "filled") {
        return "accepted";
    }
    if (lower == "cancelled" || lower == "canceled" || lower == "partiallyfilledcancelled") {
        return "canceled";
    }
    if (lower == "rejected") {
        return "rejected";
    }
    if (lower == "amended" || lower == "replaced") {
        return "replaced";
    }
    return "accepted";
}

void BybitClient::websocket_thread_func() {
    uint32_t backoff_attempt = 0; // grows on failures, resets on success

    while (!should_stop_) {
        try {
            tcp::resolver resolver(*ioc_);
            ws_ = std::make_unique<websocket::stream<ssl::stream<tcp::socket>>>(*ioc_, *ssl_ctx_);

            // Suggested Beast WS timeouts can be set anytime
            ws_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

            // SNI (ok before connect)
            if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), ws_host_.c_str())) {
                throw beast::system_error(
                    beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
                    "Failed to set SNI hostname");
            }

            // Connect TCP socket (IPv4-first, with DNS cache fallback)
            auto const& endpoints = resolve_ws_endpoints(false);
            boost::asio::connect(beast::get_lowest_layer(*ws_), endpoints);

            // Now the TCP socket is open → set TCP keepalive safely
            beast::get_lowest_layer(*ws_).set_option(net::socket_base::keep_alive(true));
            // (optional) disable Nagle if you want:
            // beast::get_lowest_layer(*ws_).set_option(net::ip::tcp::no_delay(true));

            // TLS handshake
            ws_->next_layer().handshake(ssl::stream_base::client);

            // Decorate request
            ws_->set_option(websocket::stream_base::decorator(
                [](websocket::request_type& req) {
                    req.set(http::field::user_agent, "latentspeed-bybit/1.0");
                }));

            // WS handshake
            beast::error_code ec;
            ws_->handshake(ws_host_, ws_target_, ec);
            if (ec) {
                spdlog::error("[BybitClient] WebSocket handshake error: {}", ec.message());
                if (ws_) {
                    boost::beast::error_code ec2;
                    beast::get_lowest_layer(*ws_).close(ec2);
                }
                ws_.reset(); 
                safe_ws_close(ws_);
                goto backoff_wait;
            }

            // Auth
            if (!send_websocket_auth()) {
                spdlog::error("[BybitClient] WebSocket authentication failed");
                if (ws_) {
                    boost::beast::error_code ec2;
                    beast::get_lowest_layer(*ws_).close(ec2);
                }
                ws_.reset(); 
                safe_ws_close(ws_);
                goto backoff_wait;
            }

            // Subscribe to private topics
            if (!send_websocket_subscribe({"order", "execution"})) {
                spdlog::warn("[BybitClient] WS subscribe failed (order/execution)");
                // still proceed; we might get another try after reconnect
            }

            // Mark connected/healthy and reset backoff
            ws_connected_.store(true);
            backoff_attempt = 0;
            last_ping_time_ = std::chrono::steady_clock::now();
            last_pong_time_ = last_ping_time_;
            spdlog::info("[BybitClient] WebSocket connected & authed");

            // ---- SINGLE REST CATCH-UP (only once after successful reconnect) ----
            // We do this here (not in the failure loop) to avoid resync storms.
            try {
                resync_pending_orders();
                spdlog::info("[BybitClient] REST catch-up complete after reconnect");
                backfill_recent_executions(120000 /* 120s */, "linear");
            } catch (const std::exception& e) {
                spdlog::warn("[BybitClient] REST catch-up error (continuing): {}", e.what());
            }
            // ---------------------------------------------------------------------

            // Message loop + heartbeat
            for (;;) {
                if (should_stop_) break;

                // Heartbeat: send ping if due
                const auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - last_ping_time_).count() >= PING_INTERVAL_SEC) {
                    send_websocket_ping();
                }

                // Read one frame
                beast::flat_buffer buffer;
                ws_->read(buffer, ec);

                if (ec) {
                    if (ec == websocket::error::closed) {
                        spdlog::warn("[BybitClient] WebSocket closed by peer");
                    } else {
                        spdlog::error("[BybitClient] WebSocket read error: {}", ec.message());
                    }
                    break; // exit message loop -> backoff -> reconnect
                }

                // Process frame
                const std::string msg = beast::buffers_to_string(buffer.data());
                process_websocket_message(msg);

                // WS health check: consider both last pong and any data RX activity
                const auto after = std::chrono::steady_clock::now();
                const auto healthy_since = std::max(last_pong_time_, last_rx_time_);
                if (std::chrono::duration_cast<std::chrono::seconds>(after - healthy_since).count() > PONG_TIMEOUT_SEC) {
                    spdlog::warn("[BybitClient] WS health timeout (no pong or data > {}s); will reconnect", PONG_TIMEOUT_SEC);
                    break; // exit message loop
                }
            }

            // Drop connection cleanly before reconnecting
             if (ws_) {
                boost::beast::error_code ec2;
                beast::get_lowest_layer(*ws_).close(ec2);
            }
            ws_.reset(); 
            safe_ws_close(ws_);
            ws_connected_.store(false);

        } catch (const std::exception& e) {
            spdlog::error("[BybitClient] WebSocket exception: {}", e.what());
            if (ws_) {
                boost::beast::error_code ec2;
                beast::get_lowest_layer(*ws_).close(ec2);
            }
            ws_.reset(); 
            safe_ws_close(ws_);
            ws_connected_.store(false);
        }

    backoff_wait:
        if (should_stop_) break;

        // IMPORTANT: No resync here. We only resync once *after* a successful reconnect.
        const uint32_t sleep_ms = next_backoff_ms(backoff_attempt);
        spdlog::info("[BybitClient] Reconnecting WebSocket in {} ms (attempt #{})", sleep_ms, backoff_attempt);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
}


bool BybitClient::send_websocket_auth() {
    try {
        // Bybit sample recipe: HMAC_SHA256(secret, "GET/realtime" + expiresMs)
        // Keep expires tight (now + 1000 ms).
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto expires_ms = now_ms + 5000;

        const std::string expires = std::to_string(expires_ms);
        const std::string sign_payload = std::string("GET/realtime") + expires;
        const std::string signature = hmac_sha256(api_secret_, sign_payload);

        rapidjson::Document auth_msg;
        auth_msg.SetObject();
        auto& alloc = auth_msg.GetAllocator();

        auth_msg.AddMember("op", "auth", alloc);

        rapidjson::Value args(rapidjson::kArrayType);
        args.PushBack(rapidjson::Value(api_key_.c_str(), alloc), alloc);
        args.PushBack(rapidjson::Value(expires.c_str(), alloc), alloc);
        args.PushBack(rapidjson::Value(signature.c_str(), alloc), alloc);
        auth_msg.AddMember("args", args, alloc);

        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        auth_msg.Accept(w);

        beast::error_code ec;
        ws_->text(true); // send JSON text frames
        ws_->write(net::buffer(sb.GetString(), sb.GetSize()), ec);
        if (ec) {
            spdlog::error("[BybitClient] WS auth write error: {}", ec.message());
            return false;
        }

        // Read a single response frame for auth ack; do not block forever.
        beast::flat_buffer resp_buf;
        ws_->read(resp_buf, ec);
        if (ec) {
            spdlog::error("[BybitClient] WS auth read error: {}", ec.message());
            return false;
        }

        const std::string resp = beast::buffers_to_string(resp_buf.data());
        rapidjson::Document resp_doc;
        resp_doc.Parse(resp.c_str());
        if (resp_doc.HasParseError() || !resp_doc.IsObject()) {
            spdlog::error("[BybitClient] WS auth invalid JSON response: {}", resp);
            return false;
        }

        // Bybit replies include "op":"auth" and "success":true/false
        bool ok = false;
        if (resp_doc.HasMember("success") && resp_doc["success"].IsBool()) {
            ok = resp_doc["success"].GetBool();
        }
        if (ok) {
            spdlog::info("[BybitClient] WebSocket authenticated successfully");
            return true;
        }

        spdlog::error("[BybitClient] WebSocket auth failed: {}", resp);
        return false;

    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] WebSocket auth failed: {}", e.what());
        return false;
    }
}

bool BybitClient::send_websocket_subscribe(const std::vector<std::string>& topics) {
    try {
        std::vector<std::string> want = topics;
        if (want.empty()) {
            // Private stream defaults for demo/mainnet: order + execution
            want = {"order", "execution"};
        }

        rapidjson::Document msg;
        msg.SetObject();
        auto& alloc = msg.GetAllocator();

        msg.AddMember("op", "subscribe", alloc);

        rapidjson::Value args(rapidjson::kArrayType);
        for (const auto& t : want) {
            args.PushBack(rapidjson::Value(t.c_str(), alloc), alloc);
        }
        msg.AddMember("args", args, alloc);

        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        msg.Accept(w);

        beast::error_code ec;
        ws_->text(true);
        ws_->write(net::buffer(sb.GetString(), sb.GetSize()), ec);
        if (ec) {
            spdlog::error("[BybitClient] WS subscribe write error: {}", ec.message());
            return false;
        }

        spdlog::info("[BybitClient] WS subscribe sent ({} topics)", want.size());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] WS subscribe failed: {}", e.what());
        return false;
    }
}

void BybitClient::process_websocket_message(const std::string& message) {
    last_rx_time_ = std::chrono::steady_clock::now();
    try {
        rapidjson::Document doc;
        doc.Parse(message.c_str());
        if (doc.HasParseError() || !doc.IsObject()) {
            spdlog::error("[BybitClient] Failed to parse WebSocket message");
            return;
        }

        // Generic op replies
        if (doc.HasMember("op") && doc["op"].IsString()) {
            const std::string op = doc["op"].GetString();
            if (op == "pong") {
                last_pong_time_ = std::chrono::steady_clock::now();
                spdlog::debug("[BybitClient] WS pong received");
                return;
            }
            if (op == "auth") {
                bool ok = doc.HasMember("success") && doc["success"].IsBool() && doc["success"].GetBool();
                spdlog::info("[BybitClient] WS auth ack success={}", ok);
                return;
            }
            if (op == "subscribe") {
                bool ok = doc.HasMember("success") && doc["success"].IsBool() && doc["success"].GetBool();
                spdlog::info("[BybitClient] WS subscribe ack success={}", ok);
                return;
            }
        }

        // Topic-based routing (supports dotted variants like "order.linear")
        if (doc.HasMember("topic") && doc["topic"].IsString()) {
            const std::string topic = doc["topic"].GetString();
            const bool is_order = (topic.rfind("order", 0) == 0) || (topic.find(".order") != std::string::npos) || (topic.find("order.") != std::string::npos);
            const bool is_exec  = (topic.rfind("execution", 0) == 0) || (topic.find(".execution") != std::string::npos) || (topic.find("execution.") != std::string::npos);

            // If Bybit supplies a data array, emit per-item for better granularity
            const rapidjson::Value* data_arr = get_data_array(doc);
            if (data_arr) {
                if (is_order) {
                    for (auto it = data_arr->Begin(); it != data_arr->End(); ++it) {
                        if (it->IsObject()) {
                            emit_order_snapshot(*it);
                        }
                    }
                    return;
                }
                if (is_exec) {
                    for (auto it = data_arr->Begin(); it != data_arr->End(); ++it) {
                        if (it->IsObject()) {
                            emit_execution_snapshot(*it);
                        }
                    }
                    return;
                }
            }

            // Fallback to your existing handlers if no array or custom format
            if (is_order) {
                handle_order_update_message(doc);
                return;
            }
            if (is_exec) {
                for (auto it = data_arr->Begin(); it != data_arr->End(); ++it) {
                    if (it->IsObject()) {
                        emit_execution_snapshot(*it);

                        // NEW: advance cursor from exec item if available
                        const auto& row = *it;
                        // Bybit v5: execTime is usually string or number (ms)
                        if (row.HasMember("execTime")) {
                            if (row["execTime"].IsUint64()) {
                                maybe_advance_exec_cursor(row["execTime"].GetUint64());
                            } else if (row["execTime"].IsString()) {
                                try {
                                    uint64_t t = std::stoull(row["execTime"].GetString());
                                    maybe_advance_exec_cursor(t);
                                } catch (...) {}
                            }
                        } else if (row.HasMember("time") && row["time"].IsUint64()) {
                            // Some payloads use "time"
                            maybe_advance_exec_cursor(row["time"].GetUint64());
                        }
                    }
                }
                return;
            }
        }

        // Unhandled payload (keep low noise)
        spdlog::debug("[BybitClient] WS message (unhandled): {}", message.substr(0, 256));
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] Error processing WebSocket message: {}", e.what());
    }
}

void BybitClient::backfill_recent_executions(uint64_t lookback_ms, const std::string& category_hint) {
    try {
        const uint64_t now_ms = std::stoull(get_timestamp_ms());
        const uint64_t start_ms = (now_ms > lookback_ms) ? (now_ms - lookback_ms) : 0ULL;
        const std::string cat = category_hint.empty() ? "linear" : category_hint;

        // NOTE: use perform_rest_request_locked (shared socket), not make_rest_request (which may open ad-hoc sessions)
        const std::string endpoint =
            "/v5/execution/list?category=" + cat +
            "&startTime=" + std::to_string(start_ms) +
            "&limit=200";

        std::string resp = perform_rest_request_locked("GET", endpoint, /*params_json*/"");
        rapidjson::Document doc;
        doc.Parse(resp.c_str());
        if (doc.HasParseError() || !doc.HasMember("retCode") || doc["retCode"].GetInt() != 0) {
            spdlog::warn("[BybitClient] exec backfill: bad response retCode ({}), skipping",
                         doc.HasMember("retCode") ? doc["retCode"].GetInt() : -1);
            return;
        }
        if (!doc.HasMember("result")) return;
        const auto& result = doc["result"];
        if (!result.HasMember("list") || !result["list"].IsArray()) return;

        size_t emitted = 0;
        for (const auto& row : result["list"].GetArray()) {
            emit_execution_snapshot(row);
            ++emitted;
        }
        spdlog::info("[BybitClient] exec backfill: emitted {} executions (lookback={} ms)", emitted, lookback_ms);
    } catch (const std::exception& e) {
        spdlog::warn("[BybitClient] exec backfill error: {}", e.what());
    }
}


void BybitClient::handle_order_update_message(const rapidjson::Document& doc) {
    if (!doc.HasMember("data") || !doc["data"].IsArray()) {
        return;
    }
    
    for (const auto& order_data : doc["data"].GetArray()) {
        OrderUpdate update;
        
        if (order_data.HasMember("orderLinkId")) {
            update.client_order_id = order_data["orderLinkId"].GetString();
        }
        if (order_data.HasMember("orderId")) {
            update.exchange_order_id = order_data["orderId"].GetString();
        }
        if (order_data.HasMember("orderStatus")) {
            update.status = order_data["orderStatus"].GetString();
        }
        if (order_data.HasMember("rejectReason")) {
            update.reason = order_data["rejectReason"].GetString();
        }
        if (order_data.HasMember("updatedTime")) {
            update.timestamp_ms = std::stoull(order_data["updatedTime"].GetString());
        }
        
        if (order_update_callback_) {
            order_update_callback_(update);
        }
    }
}

void BybitClient::handle_execution_message(const rapidjson::Document& doc) {
    if (!doc.HasMember("data") || !doc["data"].IsArray()) {
        return;
    }
    
    for (const auto& exec_data : doc["data"].GetArray()) {
        emit_execution_snapshot(exec_data);
    }
}

void BybitClient::emit_execution_snapshot(const rapidjson::Value& exec_data) {
    // Require execId and ensure it's a string
    if (!exec_data.HasMember("execId") || !exec_data["execId"].IsString()) {
        return;
    }

    const std::string exec_id = exec_data["execId"].GetString();

    // --- de-dup with soft cap ---
    {
        std::lock_guard<std::mutex> lk(seen_exec_mutex_);
        constexpr size_t kMaxExecIds = 50000;  // tune as you like
        if (seen_exec_ids_.size() >= kMaxExecIds) {
            seen_exec_ids_.clear();            // simple prune
            seen_exec_ids_.reserve(kMaxExecIds); // avoid rehash churn
        }
        if (!exec_id.empty()) {
            if (!seen_exec_ids_.insert(exec_id).second) {
                return; // already processed
            }
        }
    }

    FillData fill;
    fill.exec_id = exec_id;

    // Prefer client_order_id (orderLinkId); fallback to exchange orderId for manual/external actions
    if (exec_data.HasMember("orderLinkId") && exec_data["orderLinkId"].IsString()) {
        fill.client_order_id = exec_data["orderLinkId"].GetString();
    } else if (exec_data.HasMember("orderId") && exec_data["orderId"].IsString()) {
        fill.client_order_id = exec_data["orderId"].GetString();
    }

    if (exec_data.HasMember("orderId") && exec_data["orderId"].IsString()) {
        fill.exchange_order_id = exec_data["orderId"].GetString();
    }
    if (exec_data.HasMember("symbol") && exec_data["symbol"].IsString()) {
        fill.symbol = exec_data["symbol"].GetString();
    }
    if (exec_data.HasMember("side") && exec_data["side"].IsString()) {
        const std::string side = exec_data["side"].GetString();
        fill.side = (side == "Buy") ? "buy" : "sell";
    }
    if (exec_data.HasMember("execPrice") && exec_data["execPrice"].IsString()) {
        fill.price = exec_data["execPrice"].GetString();
    }
    if (exec_data.HasMember("execQty") && exec_data["execQty"].IsString()) {
        fill.quantity = exec_data["execQty"].GetString();
    }
    if (exec_data.HasMember("execFee") && exec_data["execFee"].IsString()) {
        fill.fee = exec_data["execFee"].GetString();
    }
    if (exec_data.HasMember("feeCurrency") && exec_data["feeCurrency"].IsString()) {
        fill.fee_currency = exec_data["feeCurrency"].GetString();
    } else if (exec_data.HasMember("feeRate")) {
        // heuristic default if venue omits currency but provides a rate
        fill.fee_currency = "USDT";
    }
    if (exec_data.HasMember("isMaker") && exec_data["isMaker"].IsBool()) {
        fill.liquidity = exec_data["isMaker"].GetBool() ? "maker" : "taker";
    }

    // Timestamp (string or number); also advance the exec cursor so catch-ups get smaller
    if (exec_data.HasMember("execTime")) {
        if (exec_data["execTime"].IsUint64()) {
            fill.timestamp_ms = exec_data["execTime"].GetUint64();
        } else if (exec_data["execTime"].IsString()) {
            try {
                fill.timestamp_ms = std::stoull(exec_data["execTime"].GetString());
            } catch (...) { /* ignore parse errors */ }
        }
    } else if (exec_data.HasMember("time") && exec_data["time"].IsUint64()) {
        fill.timestamp_ms = exec_data["time"].GetUint64();
    }

    // (If you added PR5’s cursor helper) keep the backfill cursor fresh
    if (fill.timestamp_ms) {
        maybe_advance_exec_cursor(fill.timestamp_ms);
    }

    if (fill_callback_) {
        fill_callback_(fill);
    }
}


void BybitClient::send_websocket_ping() {
    try {
        // Bybit expects {"op":"ping"} on private streams
        static constexpr const char* kPing = "{\"op\":\"ping\"}";
        beast::error_code ec;
        ws_->text(true);
        ws_->write(net::buffer(kPing, std::strlen(kPing)), ec);
        if (ec) {
            spdlog::warn("[BybitClient] WS ping write error: {}", ec.message());
            return;
        }
        last_ping_time_ = std::chrono::steady_clock::now();
        spdlog::debug("[BybitClient] WS ping sent");
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] Failed to send ping: {}", e.what());
    }
}

std::vector<OpenOrderBrief> BybitClient::list_open_orders(
    const std::optional<std::string>& category,
    const std::optional<std::string>& symbol,
    const std::optional<std::string>& settle_coin,
    const std::optional<std::string>& base_coin)
{
    std::vector<OpenOrderBrief> out;

    auto build_endpoint = [](const std::string& cat,
                             const std::optional<std::string>& sym,
                             const std::optional<std::string>& settle,
                             const std::optional<std::string>& base) {
        std::string ep = "/v5/order/realtime?category=" + cat;
        if (sym && !sym->empty())       ep += "&symbol="     + *sym;
        else if (settle && !settle->empty()) ep += "&settleCoin=" + *settle;
        else if (base && !base->empty())   ep += "&baseCoin="   + *base;
        // Bybit default is open orders; no need to add openOnly=0
        return ep;
    };

    struct Q { std::string endpoint; std::string cat; };
    std::vector<Q> plan;

    // If caller specified a category, honor it exactly.
    if (category && !category->empty()) {
        // On demo, most categories except linear/USDT will 10032. We'll still honor explicit asks.
        std::string settle = (settle_coin && !settle_coin->empty())
                               ? *settle_coin
                               : (is_testnet_ ? std::string("USDT") : std::string());
        plan.push_back({build_endpoint(*category, symbol, settle.empty() ? std::nullopt : std::optional<std::string>(settle), base_coin), *category});
    } else {
        // No category provided: choose a sensible default set.
        if (is_testnet_) {
            // Demo: keep it simple & supported.
            plan.push_back({build_endpoint("linear", symbol, std::string("USDT"), std::nullopt), "linear"});
        } else {
            // Mainnet: try linear USDT and USDC; spot/inverse only if symbol/base provided.
            plan.push_back({build_endpoint("linear", symbol, std::string("USDT"), std::nullopt), "linear"});
            plan.push_back({build_endpoint("linear", symbol, std::string("USDC"), std::nullopt), "linear"});
            if (symbol && !symbol->empty()) {
                plan.push_back({build_endpoint("spot", symbol, std::nullopt, std::nullopt), "spot"});
            }
            if (base_coin && !base_coin->empty()) {
                plan.push_back({build_endpoint("inverse", std::nullopt, std::nullopt, base_coin), "inverse"});
            }
        }
    }

    for (const auto& q : plan) {
        std::string body;
        try {
            // IMPORTANT: GET with querystring; do not send a JSON body for GET.
            body = perform_rest_request_locked("GET", q.endpoint, "");
        } catch (const std::exception& e) {
            spdlog::warn("[BybitClient] list_open_orders GET {} failed: {}", q.endpoint, e.what());
            continue;
        }

        rapidjson::Document doc;
        doc.Parse(body.c_str());
        if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("retCode")) {
            spdlog::warn("[BybitClient] list_open_orders: invalid JSON for {}", q.endpoint);
            continue;
        }

        const int ret = doc["retCode"].IsInt() ? doc["retCode"].GetInt() : -1;
        if (ret != 0) {
            const char* msg = (doc.HasMember("retMsg") && doc["retMsg"].IsString())
                                ? doc["retMsg"].GetString() : "";
            // On demo, 10032 ("Demo trading are not supported.") is expected for some categories; downgrade to info.
            if (ret == 10032 && is_testnet_) {
                spdlog::info("[BybitClient] list_open_orders (demo) skipped {}: retCode={} retMsg='{}'", q.endpoint, ret, msg);
            } else {
                spdlog::warn("[BybitClient] list_open_orders: retCode={} retMsg='{}' for {}", ret, msg, q.endpoint);
            }
            continue;
        }

        const rapidjson::Value* list = nullptr;
        if (doc.HasMember("result") && doc["result"].IsObject()) {
            const auto& res = doc["result"];
            if (res.HasMember("list") && res["list"].IsArray()) list = &res["list"];
            else if (res.HasMember("orders") && res["orders"].IsArray()) list = &res["orders"];
        } else if (doc.HasMember("data") && doc["data"].IsArray()) {
            list = &doc["data"];
        }

        if (!list) {
            spdlog::info("[BybitClient] list_open_orders: empty list for {}", q.endpoint);
            continue;
        }

        for (auto it = list->Begin(); it != list->End(); ++it) {
            if (!it->IsObject()) continue;

            OpenOrderBrief b;
            if (it->HasMember("orderId") && (*it)["orderId"].IsString())
                b.exchange_order_id = (*it)["orderId"].GetString();
            if (it->HasMember("orderLinkId") && (*it)["orderLinkId"].IsString())
                b.client_order_id = (*it)["orderLinkId"].GetString();
            if (b.client_order_id.empty()) continue; // we rehydrate by cl_id

            if (it->HasMember("symbol") && (*it)["symbol"].IsString())
                b.symbol = (*it)["symbol"].GetString();
            if (it->HasMember("category") && (*it)["category"].IsString())
                b.category = (*it)["category"].GetString();
            else
                b.category = q.cat;

            if (it->HasMember("side") && (*it)["side"].IsString())
                b.side = (*it)["side"].GetString(); // "Buy"/"Sell"
            if (it->HasMember("orderType") && (*it)["orderType"].IsString())
                b.order_type = (*it)["orderType"].GetString();
            if (it->HasMember("qty") && (*it)["qty"].IsString())
                b.qty = (*it)["qty"].GetString();
            if (it->HasMember("reduceOnly") && (*it)["reduceOnly"].IsBool())
                b.reduce_only = (*it)["reduceOnly"].GetBool();

            out.emplace_back(std::move(b));
        }
    }

    return out;
}



} // namespace latentspeed
