/**
 * @file binance_client.cpp
 * @brief Binance exchange client scaffold (USDT-M futures first)
 */

#include "exchange/binance_client.h"
#include <spdlog/spdlog.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <curl/curl.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <stdexcept>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/buffer.hpp>

namespace latentspeed {

bool BinanceClient::initialize(const std::string& api_key,
                               const std::string& api_secret,
                               bool testnet) {
    api_key_ = api_key;
    api_secret_ = api_secret;
    is_testnet_ = testnet;
    configure_endpoints(testnet);
    // Optional flag to enable WS-API trading later
    if (const char* v = std::getenv("LATENTSPEED_BINANCE_USE_WS_TRADE")) {
        std::string s(v);
        for (auto& c : s) c = static_cast<char>(::tolower(c));
        use_ws_trading_ = (s == "1" || s == "true" || s == "yes" || s == "on");
    }
    spdlog::info("[BinanceClient] Initialized (testnet: {}, ws-trade: {})", is_testnet_, use_ws_trading_);
    return true;
}

bool BinanceClient::connect() {
    // Create listen key and start keepalive thread; start user-data WS consumer
    std::string lk;
    if (!create_listen_key(lk)) {
        spdlog::warn("[BinanceClient] Failed to create listenKey; updates/fills may be delayed");
    } else {
        active_listen_key_ = lk;
        stop_.store(false);
        listenkey_thread_ = std::make_unique<std::thread>(&BinanceClient::listenkey_keepalive_loop, this);
        spdlog::info("[BinanceClient] listenKey acquired");
        ws_thread_ = std::make_unique<std::thread>(&BinanceClient::ws_user_thread, this);
    }
    connected_.store(true);
    return true;
}

void BinanceClient::disconnect() {
    connected_.store(false);
    stop_.store(true);
    if (listenkey_thread_ && listenkey_thread_->joinable()) {
        listenkey_thread_->join();
    }
    if (ws_thread_ && ws_thread_->joinable()) {
        ws_thread_->join();
    }
    spdlog::info("[BinanceClient] Disconnected (stub)");
}

bool BinanceClient::is_connected() const {
    return connected_.load();
}

OrderResponse BinanceClient::place_order(const OrderRequest& request) {
    // Build query params for POST /fapi/v1/order
    std::ostringstream q;
    // Apply filters and rounding
    std::string symbol = request.symbol;
    SymbolFilters f{};
    if (get_symbol_filters(symbol, f)) {
        // Round quantity and price
        std::string q_str = request.quantity;
        std::string p_str = request.price.has_value() ? *request.price : std::string();
        try {
            if (!q_str.empty() && f.step_size > 0) {
                double qv = std::stod(q_str); double step = f.step_size;
                double adj = std::floor(qv / step) * step;
                q_str = format_decimal(adj, f.qty_decimals);
            }
            if (!p_str.empty() && f.tick_size > 0) {
                double pv = std::stod(p_str); double tick = f.tick_size;
                double adj = std::floor(pv / tick) * tick;
                p_str = format_decimal(adj, f.price_decimals);
            }
        } catch (...) {
            // Leave as-is if parse fails
        }
        q << "symbol=" << symbol;
        q << "&side=" << map_side(request.side);
        bool post_only = false;
        if (request.time_in_force.has_value()) {
            auto tif = *request.time_in_force;
            std::string tif_lower = tif; for (auto& c : tif_lower) c = static_cast<char>(::tolower(c));
            post_only = (tif_lower == "postonly" || tif_lower == "po" || tif_lower == "gtx");
        }
        q << "&type=" << map_type(request.order_type, post_only);
        if (request.reduce_only) q << "&reduceOnly=true";
        if (!q_str.empty()) q << "&quantity=" << q_str;
        if (!p_str.empty() && request.order_type == "limit") q << "&price=" << p_str;
        if (request.order_type != std::string("market")) {
            const auto tif_str = map_time_in_force(request.time_in_force, post_only);
            if (!tif_str.empty()) q << "&timeInForce=" << tif_str;
        }
        if (!request.client_order_id.empty()) q << "&newClientOrderId=" << request.client_order_id;
        if (auto it = request.extra_params.find("triggerPrice"); it != request.extra_params.end()) {
            q << "&stopPrice=" << it->second;
            // Determine STOP vs TAKE_PROFIT based on triggerDirection and side
            std::string dir;
            if (auto itd = request.extra_params.find("triggerDirection"); itd != request.extra_params.end()) dir = itd->second;
            const bool is_buy = (map_side(request.side) == "BUY");
            bool is_stop = false;
            // Binance semantics: triggerDirection "1" (rising) = STOP, "2" (falling) = TAKE_PROFIT
            if (dir == "1") is_stop = true; 
            else if (dir == "2") is_stop = false; 
            else {
                // Fallback heuristic: if we don't know, use side-based default to keep previous behavior
                is_stop = !is_buy; 
            }
            if (request.order_type == "market") {
                q << (is_stop ? "&type=STOP_MARKET" : "&type=TAKE_PROFIT_MARKET");
            } else {
                q << (is_stop ? "&type=STOP" : "&type=TAKE_PROFIT");
            }
            // Default workingType if not provided
            if (request.extra_params.find("workingType") == request.extra_params.end()) {
                q << "&workingType=MARK_PRICE";
            }
        }
    } else {
        // Fallback: no filters
        q << "symbol=" << symbol;
        q << "&side=" << map_side(request.side);
        bool post_only = false;
        if (request.time_in_force.has_value()) {
            auto tif = *request.time_in_force;
            std::string tif_lower = tif; for (auto& c : tif_lower) c = static_cast<char>(::tolower(c));
            post_only = (tif_lower == "postonly" || tif_lower == "po" || tif_lower == "gtx");
        }
        q << "&type=" << map_type(request.order_type, post_only);
        if (request.reduce_only) q << "&reduceOnly=true";
        if (!request.quantity.empty()) q << "&quantity=" << request.quantity;
        if (request.price.has_value() && request.order_type == "limit") q << "&price=" << *request.price;
        if (request.order_type != std::string("market")) {
            const auto tif_str = map_time_in_force(request.time_in_force, post_only);
            if (!tif_str.empty()) q << "&timeInForce=" << tif_str;
        }
        if (!request.client_order_id.empty()) q << "&newClientOrderId=" << request.client_order_id;
        if (auto it = request.extra_params.find("triggerPrice"); it != request.extra_params.end()) {
            q << "&stopPrice=" << it->second;
            if (request.order_type == "market") q << "&type=STOP_MARKET";
        }
    }
    q << "&side=" << map_side(request.side);
    bool post_only = false;
    if (request.time_in_force.has_value()) {
        auto tif = *request.time_in_force;
        std::string tif_lower = tif; for (auto& c : tif_lower) c = static_cast<char>(::tolower(c));
        post_only = (tif_lower == "postonly" || tif_lower == "po" || tif_lower == "gtx");
    }
    q << "&type=" << map_type(request.order_type, post_only);

    // Reduce-only for futures
    if (request.reduce_only) {
        q << "&reduceOnly=true";
    }

    // Quantity and price
    if (!request.quantity.empty()) {
        q << "&quantity=" << request.quantity;
    }
    if (request.price.has_value() && request.order_type == "limit") {
        q << "&price=" << *request.price;
    }

    // Time in force (skip for MARKET/STOP_MARKET/TP_MARKET base)
    if (request.order_type != std::string("market")) {
        const auto tif_str = map_time_in_force(request.time_in_force, post_only);
        if (!tif_str.empty()) q << "&timeInForce=" << tif_str;
    }

    // Client order id
    if (!request.client_order_id.empty()) {
        q << "&newClientOrderId=" << request.client_order_id;
    }

    // Stop/TP mapping (basic): triggerPrice -> stopPrice if present
        if (auto it = request.extra_params.find("triggerPrice"); it != request.extra_params.end()) {
            q << "&stopPrice=" << it->second;
            std::string dir;
            if (auto itd = request.extra_params.find("triggerDirection"); itd != request.extra_params.end()) dir = itd->second;
            const bool is_buy = (map_side(request.side) == "BUY");
            bool is_stop = false;
            if (dir == "1") is_stop = true; 
            else if (dir == "2") is_stop = false; 
            else { is_stop = !is_buy; }
            if (request.order_type == "market") {
                q << (is_stop ? "&type=STOP_MARKET" : "&type=TAKE_PROFIT_MARKET");
            } else {
                q << (is_stop ? "&type=STOP" : "&type=TAKE_PROFIT");
            }
            if (request.extra_params.find("workingType") == request.extra_params.end()) {
                q << "&workingType=MARK_PRICE";
            }
        }

    long status = 0; std::string body;
    if (!rest_request("POST", "/fapi/v1/order", q.str(), true, status, body)) {
        // One-shot retry on transport (curl) errors
        if (status == 0) {
            spdlog::warn("[BinanceClient] place_order initial POST failed ('{}'); retrying once", body);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (!rest_request("POST", "/fapi/v1/order", q.str(), true, status, body)) {
                std::string msg = body.empty() ? std::string("HTTP error") : body.substr(0, 200);
                return OrderResponse{false, msg, std::nullopt, request.client_order_id, std::nullopt, {}};
            }
        } else {
            // Surface server error payload if present
            std::string msg = body.empty() ? (std::string("http ") + std::to_string(status)) : body.substr(0, 200);
            return OrderResponse{false, msg, std::nullopt, request.client_order_id, std::nullopt, {}};
        }
    }

    rapidjson::Document d; d.Parse(body.c_str());
    if (d.HasParseError()) {
        return OrderResponse{false, "Parse error", std::nullopt, request.client_order_id, std::nullopt, {}};
    }
    if (d.HasMember("code") && d["code"].IsInt()) {
        std::string msg = d.HasMember("msg") && d["msg"].IsString() ? d["msg"].GetString() : "error";
        return OrderResponse{false, msg, std::nullopt, request.client_order_id, std::nullopt, {}};
    }
    OrderResponse resp{true, "ok", std::nullopt, std::nullopt, std::nullopt, {}};
    if (d.HasMember("orderId")) resp.exchange_order_id = std::to_string(d["orderId"].GetInt64());
    if (d.HasMember("clientOrderId")) resp.client_order_id = d["clientOrderId"].GetString();
    if (d.HasMember("status") && d["status"].IsString()) resp.status = d["status"].GetString();
    // Cache pending snapshot for lookups
    if (resp.client_order_id.has_value()) {
        std::lock_guard<std::mutex> lk(orders_mutex_);
        pending_orders_[*resp.client_order_id] = request;
    }
    return resp;
}

OrderResponse BinanceClient::cancel_order(const std::string& client_order_id,
                                          const std::optional<std::string>& symbol,
                                          const std::optional<std::string>& exchange_order_id) {
    if (!symbol.has_value()) {
        return OrderResponse{false, "symbol required for cancel on Binance", std::nullopt, client_order_id, std::nullopt, {}};
    }
    std::ostringstream q;
    q << "symbol=" << *symbol;
    if (exchange_order_id.has_value()) {
        q << "&orderId=" << *exchange_order_id;
    } else {
        q << "&origClientOrderId=" << client_order_id;
    }
    long status = 0; std::string body;
    if (!rest_request("DELETE", "/fapi/v1/order", q.str(), true, status, body)) {
        return OrderResponse{false, "HTTP error", std::nullopt, client_order_id, std::nullopt, {}};
    }
    rapidjson::Document d; d.Parse(body.c_str());
    if (d.HasParseError()) {
        return OrderResponse{false, "Parse error", std::nullopt, client_order_id, std::nullopt, {}};
    }
    if (d.HasMember("code") && d["code"].IsInt()) {
        std::string msg = d.HasMember("msg") && d["msg"].IsString() ? d["msg"].GetString() : "error";
        return OrderResponse{false, msg, std::nullopt, client_order_id, std::nullopt, {}};
    }
    OrderResponse resp{true, "ok", std::nullopt, std::nullopt, std::nullopt, {}};
    if (d.HasMember("orderId")) resp.exchange_order_id = std::to_string(d["orderId"].GetInt64());
    if (d.HasMember("clientOrderId")) resp.client_order_id = d["clientOrderId"].GetString();
    if (d.HasMember("status") && d["status"].IsString()) resp.status = d["status"].GetString();
    if (resp.client_order_id.has_value()) {
        std::lock_guard<std::mutex> lk(orders_mutex_);
        pending_orders_.erase(*resp.client_order_id);
    }
    return resp;
}

OrderResponse BinanceClient::modify_order(const std::string& client_order_id,
                                          const std::optional<std::string>& /*new_quantity*/,
                                          const std::optional<std::string>& /*new_price*/) {
    // Cancel+new fallback: look up original snapshot to derive symbol and base params
    OrderRequest base;
    {
        std::lock_guard<std::mutex> lk(orders_mutex_);
        auto it = pending_orders_.find(client_order_id);
        if (it == pending_orders_.end()) {
            return OrderResponse{false, "modify: original not found", std::nullopt, client_order_id, std::nullopt, {}};
        }
        base = it->second;
    }

    // First cancel original
    auto cancel_resp = cancel_order(client_order_id, base.symbol, std::nullopt);
    if (!cancel_resp.success) {
        // If idempotent missing, continue
        std::string msg_lower = cancel_resp.message; for (auto& c : msg_lower) c = static_cast<char>(::tolower(c));
        bool missing = msg_lower.find("not found") != std::string::npos || msg_lower.find("unknown order") != std::string::npos;
        if (!missing) return OrderResponse{false, std::string("modify cancel failed: ") + cancel_resp.message, std::nullopt, client_order_id, std::nullopt, {}};
    }

    // Build new order request using overrides
    OrderRequest newer = base;
    newer.client_order_id = client_order_id; // reuse id to keep upstream link
    // Note: new_quantity/new_price not provided via interface here; engine passes via tags -> service converts
    // Fallback: rely on service-provided overrides in base.extra_params if present
    if (auto it = newer.extra_params.find("replace_new_size"); it != newer.extra_params.end()) newer.quantity = it->second;
    if (auto it = newer.extra_params.find("replace_new_price"); it != newer.extra_params.end()) newer.price = it->second;

    auto place_resp = place_order(newer);
    if (!place_resp.success) {
        return OrderResponse{false, std::string("modify place failed: ") + place_resp.message, std::nullopt, client_order_id, std::nullopt, {}};
    }
    return OrderResponse{true, "modified", place_resp.exchange_order_id, client_order_id, place_resp.status, {}};
}

OrderResponse BinanceClient::query_order(const std::string& client_order_id) {
    // Prefer symbol from local pending map
    std::optional<std::string> symbol_opt;
    {
        std::lock_guard<std::mutex> lk(orders_mutex_);
        auto it = pending_orders_.find(client_order_id);
        if (it != pending_orders_.end()) symbol_opt = it->second.symbol;
    }
    if (symbol_opt.has_value()) {
        long status = 0; std::string body; std::ostringstream q;
        q << "symbol=" << *symbol_opt << "&origClientOrderId=" << client_order_id;
    if (!rest_request("GET", "/fapi/v1/order", q.str(), true, status, body)) {
        return OrderResponse{false, "HTTP error", std::nullopt, client_order_id, std::nullopt, {}};
    }
        rapidjson::Document d; d.Parse(body.c_str());
        if (d.HasMember("code") && d["code"].IsInt()) {
            std::string msg = d.HasMember("msg") && d["msg"].IsString() ? d["msg"].GetString() : "error";
            return OrderResponse{false, msg, std::nullopt, client_order_id, std::nullopt, {}};
        }
        OrderResponse resp{true, "ok", std::nullopt, client_order_id, std::nullopt, {}};
        if (d.HasMember("orderId")) resp.exchange_order_id = std::to_string(d["orderId"].GetInt64());
        if (d.HasMember("status") && d["status"].IsString()) resp.status = d["status"].GetString();
        if (d.HasMember("symbol") && d["symbol"].IsString()) resp.extra_data["symbol"] = d["symbol"].GetString();
        resp.extra_data["exchange"] = "binance";
        resp.extra_data["category"] = "linear";
        return resp;
    }
    // Fallback: open orders scan
    long status = 0; std::string body;
    if (!rest_request("GET", "/fapi/v1/openOrders", "", true, status, body)) {
        return OrderResponse{false, "HTTP error", std::nullopt, client_order_id, std::nullopt, {}};
    }
    rapidjson::Document d; d.Parse(body.c_str());
    if (!d.IsArray()) return OrderResponse{false, "unexpected response", std::nullopt, client_order_id, std::nullopt, {}};
    for (auto& v : d.GetArray()) {
        if (v.HasMember("clientOrderId") && v["clientOrderId"].IsString() && client_order_id == v["clientOrderId"].GetString()) {
            OrderResponse resp{true, "ok", std::nullopt, client_order_id, std::nullopt, {}};
            if (v.HasMember("orderId")) resp.exchange_order_id = std::to_string(v["orderId"].GetInt64());
            if (v.HasMember("status") && v["status"].IsString()) resp.status = v["status"].GetString();
            if (v.HasMember("symbol") && v["symbol"].IsString()) resp.extra_data["symbol"] = v["symbol"].GetString();
            resp.extra_data["exchange"] = "binance";
            resp.extra_data["category"] = "linear";
            return resp;
        }
    }
    return OrderResponse{false, "not found", std::nullopt, client_order_id, std::nullopt, {}};
}

bool BinanceClient::subscribe_to_orders(const std::vector<std::string>& /*symbols*/) {
    // Already acquired listenKey in connect(); full WS connection parsing can be added next
    if (active_listen_key_.empty()) {
        std::string lk;
        if (!create_listen_key(lk)) return false;
        active_listen_key_ = lk;
    }
    spdlog::info("[BinanceClient] subscribe_to_orders: listenKey ready ({} chars)", active_listen_key_.size());
    return true;
}

std::vector<OpenOrderBrief> BinanceClient::list_open_orders(
    const std::optional<std::string>& /*category*/,
    const std::optional<std::string>& symbol,
    const std::optional<std::string>& /*settle_coin*/,
    const std::optional<std::string>& /*base_coin*/) {
    std::ostringstream q;
    if (symbol.has_value()) q << "symbol=" << *symbol;
    long status = 0; std::string body;
    if (!rest_request("GET", "/fapi/v1/openOrders", q.str(), true, status, body)) {
        spdlog::warn("[BinanceClient] list_open_orders http error status={}", status);
        return {};
    }
    rapidjson::Document d; d.Parse(body.c_str());
    std::vector<OpenOrderBrief> out;
    if (!d.IsArray()) return out;
    for (auto& v : d.GetArray()) {
        OpenOrderBrief b;
        if (v.HasMember("clientOrderId") && v["clientOrderId"].IsString()) b.client_order_id = v["clientOrderId"].GetString();
        if (v.HasMember("orderId")) b.exchange_order_id = std::to_string(v["orderId"].GetInt64());
        if (v.HasMember("symbol") && v["symbol"].IsString()) b.symbol = v["symbol"].GetString();
        if (v.HasMember("side") && v["side"].IsString()) b.side = v["side"].GetString();
        if (v.HasMember("type") && v["type"].IsString()) b.order_type = v["type"].GetString();
        if (v.HasMember("origQty") && v["origQty"].IsString()) b.qty = v["origQty"].GetString();
        if (v.HasMember("reduceOnly") && v["reduceOnly"].IsBool()) b.reduce_only = v["reduceOnly"].GetBool();
        b.category = "linear";
        out.push_back(std::move(b));
    }
    return out;
}

} // namespace latentspeed

// ==== Private helpers ========================================================

namespace latentspeed {

void BinanceClient::configure_endpoints(bool testnet) {
    if (testnet) {
        rest_base_ = "https://testnet.binancefuture.com"; // UM Futures testnet
        ws_user_base_ = "wss://stream.binancefuture.com";  // user data stream base
    } else {
        rest_base_ = "https://fapi.binance.com";          // UM Futures mainnet
        ws_user_base_ = "wss://fstream.binance.com";
    }
}

uint64_t BinanceClient::timestamp_ms() const {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string BinanceClient::hmac_sha256(const std::string& key, const std::string& data) const {
    unsigned char* digest = HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
                                 reinterpret_cast<const unsigned char*>(data.data()), data.size(),
                                 nullptr, nullptr);
    static const char* hex = "0123456789abcdef";
    std::string out; out.resize(64);
    for (int i = 0; i < 32; ++i) {
        out[2*i]   = hex[(digest[i] >> 4) & 0xF];
        out[2*i+1] = hex[digest[i] & 0xF];
    }
    return out;
}

static size_t curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    std::string* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), total);
    return total;
}

bool BinanceClient::rest_request(const std::string& method,
                                 const std::string& path,
                                 const std::string& query,
                                 bool signed_req,
                                 long& http_status,
                                  std::string& response_body) {
    // Basic request throttling
    rest_rate_limiter_.throttle();
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    std::string url = rest_base_ + path;
    std::string body;
    std::string full_query = query;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, (std::string("X-MBX-APIKEY: ") + api_key_).c_str());

    if (signed_req) {
        if (!full_query.empty()) full_query += "&";
        full_query += "timestamp=" + std::to_string(timestamp_ms());
        full_query += "&recvWindow=5000";
        const std::string sig = hmac_sha256(api_secret_, full_query);
        full_query += "&signature=" + sig;
    }

    if (method == "GET" || method == "DELETE") {
        if (!full_query.empty()) url += "?" + full_query;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, full_query.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, full_query.size());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded"));
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        const char* err = curl_easy_strerror(res);
        spdlog::warn("[BinanceClient] CURL error on {}{}: {}", rest_base_, path, err);
        // Surface the curl error text to caller for better diagnostics
        response_body.assign("CURL ");
        response_body.append(err ? err : "unknown");
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    if (http_status < 200 || http_status >= 300) {
        // Log non-2xx with short body snippet to help debug
        std::string snippet = response_body.substr(0, 240);
        for (auto& ch : snippet) { if (ch == '\n' || ch == '\r') ch = ' '; }
        spdlog::warn("[BinanceClient] HTTP {} on {}{} body='{}'", http_status, rest_base_, path, snippet);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return (http_status >= 200 && http_status < 300);
}

bool BinanceClient::create_listen_key(std::string& out_listen_key) {
    long status = 0; std::string body;
    if (!rest_request("POST", "/fapi/v1/listenKey", "", false, status, body)) {
        spdlog::warn("[BinanceClient] listenKey POST failed: status={}", status);
        return false;
    }
    rapidjson::Document d; d.Parse(body.c_str());
    if (!d.IsObject() || !d.HasMember("listenKey") || !d["listenKey"].IsString()) return false;
    out_listen_key = d["listenKey"].GetString();
    return true;
}

bool BinanceClient::keepalive_listen_key(const std::string& listen_key) {
    long status = 0; std::string body;
    std::string q = std::string("listenKey=") + listen_key;
    return rest_request("PUT", "/fapi/v1/listenKey", q, false, status, body);
}

// Symbol filters retrieval and rounding helpers
bool BinanceClient::get_symbol_filters(const std::string& symbol, SymbolFilters& out) {
    std::lock_guard<std::mutex> lock(filters_mtx_);
    auto it = filters_.find(symbol);
    if (it != filters_.end()) { out = it->second; return true; }
    long status = 0; std::string body;
    std::string q = std::string("symbol=") + symbol;
    if (!rest_request("GET", "/fapi/v1/exchangeInfo", q, false, status, body)) {
        spdlog::warn("[BinanceClient] exchangeInfo http error status={}", status);
        return false;
    }
    rapidjson::Document d; d.Parse(body.c_str());
    if (!d.IsObject() || !d.HasMember("symbols") || !d["symbols"].IsArray()) return false;
    for (auto& s : d["symbols"].GetArray()) {
        if (!s.HasMember("symbol") || !s["symbol"].IsString()) continue;
        if (symbol != s["symbol"].GetString()) continue;
        SymbolFilters f{};
        if (s.HasMember("filters") && s["filters"].IsArray()) {
            for (auto& fil : s["filters"].GetArray()) {
                if (!fil.HasMember("filterType")) continue;
                std::string ft = fil["filterType"].GetString();
                if (ft == "PRICE_FILTER" && fil.HasMember("tickSize") && fil["tickSize"].IsString()) {
                    std::string ts = fil["tickSize"].GetString(); f.tick_size = std::stod(ts); f.price_decimals = decimals_from_step(ts);
                } else if (ft == "LOT_SIZE") {
                    if (fil.HasMember("stepSize") && fil["stepSize"].IsString()) { std::string ss = fil["stepSize"].GetString(); f.step_size = std::stod(ss); f.qty_decimals = decimals_from_step(ss);} 
                    if (fil.HasMember("minQty") && fil["minQty"].IsString()) { f.min_qty = std::stod(fil["minQty"].GetString()); }
                } else if ((ft == "MIN_NOTIONAL" || ft == "NOTIONAL") && fil.HasMember("notional") && fil["notional"].IsString()) {
                    f.min_notional = std::stod(fil["notional"].GetString());
                }
            }
        }
        filters_[symbol] = f; out = f; return true;
    }
    return false;
}

int BinanceClient::decimals_from_step(const std::string& step) {
    auto pos = step.find('.');
    if (pos == std::string::npos) return 0;
    int dec = static_cast<int>(step.size() - pos - 1);
    // Strip trailing zeros impact
    int end = static_cast<int>(step.size()) - 1;
    while (end > static_cast<int>(pos) && step[end] == '0') { --end; --dec; }
    if (dec < 0) dec = 0;
    return dec;
}

std::string BinanceClient::trim_zeros(std::string s) {
    auto pos = s.find('.');
    if (pos == std::string::npos) return s;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s.empty()) s = "0";
    return s;
}

std::string BinanceClient::format_decimal(double v, int decimals) {
    if (!std::isfinite(v)) return std::string();
    std::ostringstream oss; oss.setf(std::ios::fixed); oss << std::setprecision(decimals) << v;
    return trim_zeros(oss.str());
}

// WebSocket user data consumer
void BinanceClient::ws_user_thread() {
    using tcp = boost::asio::ip::tcp;
    namespace ssl = boost::asio::ssl;
    namespace websocket = boost::beast::websocket;
    while (!stop_.load()) {
        try {
            if (active_listen_key_.empty()) { std::this_thread::sleep_for(std::chrono::seconds(2)); continue; }
            // Parse host from ws_user_base_
            std::string host;
            if (ws_user_base_.rfind("wss://", 0) == 0) host = ws_user_base_.substr(6); else host = ws_user_base_;
            auto slash = host.find('/'); if (slash != std::string::npos) host = host.substr(0, slash);
            const std::string port = "443";
            const std::string target = std::string("/ws/") + active_listen_key_;

            boost::asio::io_context ioc;
            ssl::context ctx(ssl::context::tlsv12_client);
            tcp::resolver resolver(ioc);
            auto results = resolver.resolve(host, port);

            ssl::stream<tcp::socket> ssl_stream(ioc, ctx);
            boost::asio::connect(ssl_stream.next_layer(), results.begin(), results.end());
            if(!SSL_set_tlsext_host_name(ssl_stream.native_handle(), host.c_str())) {
                throw std::runtime_error("SNI set failed");
            }
            ssl_stream.handshake(ssl::stream_base::client);

            websocket::stream<ssl::stream<tcp::socket>> ws(std::move(ssl_stream));
            ws.set_option(websocket::stream_base::timeout::suggested(boost::beast::role_type::client));
            ws.handshake(host, target);

            boost::beast::flat_buffer buffer;
            ws_healthy_.store(true);
            ws_backoff_attempt_ = 0;
            while (!stop_.load()) {
                buffer.clear();
                ws.read(buffer);
                auto bufs = buffer.data();
                std::string data(
                    boost::asio::buffers_begin(bufs),
                    boost::asio::buffers_end(bufs)
                );
                process_user_ws_message(data);
            }
        } catch (const std::exception& e) {
            spdlog::warn("[BinanceClient] WS user stream error: {}", e.what());
            ws_healthy_.store(false);
            // Exponential backoff with cap ~30s
            uint32_t delay_ms = 1000u << std::min<uint32_t>(ws_backoff_attempt_, 5u);
            if (delay_ms > 30000u) delay_ms = 30000u;
            ++ws_backoff_attempt_;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }
}

void BinanceClient::process_user_ws_message(const std::string& msg) {
    rapidjson::Document d; d.Parse(msg.c_str());
    if (!d.IsObject()) return;
    // UM Futures uses e: eventType, E: eventTime, o: order update
    if (d.HasMember("e") && d["e"].IsString()) {
        std::string et = d["e"].GetString();
        if (et == "ORDER_TRADE_UPDATE") {
            const auto now_ms = timestamp_ms();
            if (!d.HasMember("o") || !d["o"].IsObject()) return;
            const auto& o = d["o"];
            OrderUpdate u{};
            if (o.HasMember("c") && o["c"].IsString()) u.client_order_id = o["c"].GetString();
            if (o.HasMember("i")) u.exchange_order_id = std::to_string(o["i"].GetInt64());
            if (o.HasMember("X") && o["X"].IsString()) {
                std::string s = o["X"].GetString();
                // Map to service-expected lowercase
                if (s == "NEW") u.status = "new";
                else if (s == "PARTIALLY_FILLED") u.status = "partiallyfilled";
                else if (s == "FILLED") u.status = "filled";
                else if (s == "CANCELED") u.status = "cancelled";
                else if (s == "EXPIRED" || s == "EXPIRED_IN_MATCH") u.status = "canceled";
                else if (s == "REJECTED") u.status = "rejected";
                else u.status = s; // pass-through
            }
            u.reason.clear();
            u.timestamp_ms = d.HasMember("E") && d["E"].IsInt64() ? static_cast<uint64_t>(d["E"].GetInt64()) : now_ms;

            // Fill info when trade occurred
            if (o.HasMember("x") && o["x"].IsString() && std::string(o["x"].GetString()) == "TRADE") {
                FillData f{};
                f.client_order_id = u.client_order_id;
                f.exchange_order_id = u.exchange_order_id;
                if (o.HasMember("s") && o["s"].IsString()) f.symbol = o["s"].GetString();
                if (o.HasMember("S") && o["S"].IsString()) f.side = o["S"].GetString();
                if (o.HasMember("L") && o["L"].IsString()) f.price = o["L"].GetString();
                if (o.HasMember("l") && o["l"].IsString()) f.quantity = o["l"].GetString();
                if (o.HasMember("n") && o["n"].IsString()) f.fee = o["n"].GetString(); else f.fee = "0";
                if (o.HasMember("N") && o["N"].IsString()) f.fee_currency = o["N"].GetString();
                if (o.HasMember("m") && o["m"].IsBool()) f.liquidity = o["m"].GetBool() ? std::string("maker") : std::string("taker");
                f.timestamp_ms = u.timestamp_ms;
                // Exec id for dedupe
                if (o.HasMember("t")) f.exec_id = std::to_string(o["t"].GetInt64());
                else f.exec_id = u.exchange_order_id + ":" + f.price + ":" + f.quantity + ":" + std::to_string(f.timestamp_ms);
                {
                    std::lock_guard<std::mutex> lk(seen_exec_mutex_);
                    if (!f.exec_id.empty()) {
                        if (seen_exec_ids_.find(f.exec_id) != seen_exec_ids_.end()) {
                            return; // duplicate
                        }
                        seen_exec_ids_.insert(f.exec_id);
                    }
                }
                u.fill = f;
                if (fill_callback_) fill_callback_(f);
            }
            // Cleanup pending on terminal status
            if (u.status == "filled" || u.status == "cancelled" || u.status == "canceled") {
                std::lock_guard<std::mutex> lk(orders_mutex_);
                pending_orders_.erase(u.client_order_id);
            }
            if (order_update_callback_) order_update_callback_(u);
        }
    }
}

void BinanceClient::listenkey_keepalive_loop() {
    // Binance suggests ~30min keepalive; run every 20 minutes (delay first to avoid early failure)
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(20min);
    while (!stop_.load()) {
        if (!active_listen_key_.empty()) {
            if (!keepalive_listen_key(active_listen_key_)) {
                spdlog::warn("[BinanceClient] listenKey keepalive failed; attempting recreate");
                std::string lk;
                if (create_listen_key(lk)) {
                    active_listen_key_ = lk;
                }
            }
        }
        std::this_thread::sleep_for(20min);
    }
}

std::string BinanceClient::map_side(const std::string& side_lower) const {
    std::string s = side_lower; for (auto& c : s) c = static_cast<char>(::tolower(c));
    return (s == "buy") ? "BUY" : "SELL";
}

std::string BinanceClient::map_type(const std::string& type_lower, bool post_only) const {
    std::string t = type_lower; for (auto& c : t) c = static_cast<char>(::tolower(c));
    if (post_only) return "LIMIT"; // enforce via GTX TIF
    if (t == "limit") return "LIMIT";
    if (t == "market") return "MARKET";
    return "LIMIT";
}

std::string BinanceClient::map_time_in_force(const std::optional<std::string>& tif_opt, bool post_only) const {
    if (post_only) return "GTX";
    if (!tif_opt.has_value()) return "";
    std::string t = *tif_opt; for (auto& c : t) c = static_cast<char>(::toupper(c));
    if (t == "GTC" || t == "IOC" || t == "FOK" || t == "GTX") return t;
    if (t == "PO" || t == "POST_ONLY") return "GTX";
    return "";
}

} // namespace latentspeed
