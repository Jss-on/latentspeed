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

namespace latentspeed {

BybitClient::BybitClient() 
    : ioc_(std::make_unique<net::io_context>()),
      ssl_ctx_(std::make_unique<ssl::context>(ssl::context::tlsv12_client)) {
    
    // Configure SSL context
    ssl_ctx_->set_default_verify_paths();
    ssl_ctx_->set_verify_mode(ssl::verify_peer);
}

BybitClient::~BybitClient() {
    disconnect();
}

bool BybitClient::initialize(const std::string& api_key, 
                             const std::string& api_secret,
                             bool testnet) {
    try {
        api_key_ = api_key;
        api_secret_ = api_secret;
        is_testnet_ = testnet;
        
        if (testnet) {
            // Bybit testnet/demo endpoints
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
        
        params.AddMember("orderLinkId", rapidjson::Value(request.client_order_id.c_str(), allocator), allocator);
        
        // Convert to JSON string
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        params.Accept(writer);
        std::string params_json = buffer.GetString();
        
        // Store pending order
        {
            std::lock_guard<std::mutex> lock(orders_mutex_);
            pending_orders_[request.client_order_id] = request;
        }
        
        // Make REST request
        std::string response = make_rest_request("POST", "/v5/order/create", params_json);
        
        // Parse response
        return parse_order_response(response);
        
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] Failed to place order: {}", e.what());
        return OrderResponse{false, e.what()};
    }
}

OrderResponse BybitClient::cancel_order(const std::string& client_order_id,
                                       const std::optional<std::string>& symbol) {
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
        
        // Convert to JSON string
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        params.Accept(writer);
        std::string params_json = buffer.GetString();
        
        // Make REST request
        std::string response = make_rest_request("POST", "/v5/order/cancel", params_json);
        
        // Parse response
        return parse_order_response(response);
        
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] Failed to cancel order: {}", e.what());
        return OrderResponse{false, e.what()};
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
        return OrderResponse{false, e.what()};
    }
}

OrderResponse BybitClient::query_order(const std::string& client_order_id) {
    try {
        // Build query parameters
        rapidjson::Document params;
        params.SetObject();
        auto& allocator = params.GetAllocator();
        
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
        return OrderResponse{false, e.what()};
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
        // Create TCP resolver and socket
        tcp::resolver resolver(*ioc_);
        ssl::stream<tcp::socket> stream(*ioc_, *ssl_ctx_);
        
        // Set SNI hostname
        if (!SSL_set_tlsext_host_name(stream.native_handle(), rest_host_.c_str())) {
            throw beast::system_error(
                beast::error_code(static_cast<int>(::ERR_get_error()),
                net::error::get_ssl_category()),
                "Failed to set SNI hostname");
        }
        
        // Connect to the server
        auto const results = resolver.resolve(rest_host_, rest_port_);
        boost::asio::connect(beast::get_lowest_layer(stream), results);
        stream.handshake(ssl::stream_base::client);
        
        // Prepare request
        http::request<http::string_body> req;
        req.method(method == "GET" ? http::verb::get : http::verb::post);
        req.target(endpoint);
        req.set(http::field::host, rest_host_);
        req.set(http::field::user_agent, "LatentSpeed/1.0");
        req.set(http::field::content_type, "application/json");
        
        // Add authentication headers
        std::string timestamp = get_timestamp_ms();
        std::string recv_window = "5000";
        
        // Sign the request
        std::string sign_payload = timestamp + api_key_ + recv_window;
        if (!params_json.empty() && method == "POST") {
            sign_payload += params_json;
            req.body() = params_json;
        }
        
        std::string signature = hmac_sha256(api_secret_, sign_payload);
        
        req.set("X-BAPI-API-KEY", api_key_);
        req.set("X-BAPI-TIMESTAMP", timestamp);
        req.set("X-BAPI-SIGN", signature);
        req.set("X-BAPI-RECV-WINDOW", recv_window);
        
        req.prepare_payload();
        
        // Send the request
        http::write(stream, req);
        
        // Receive the response
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);
        
        // Gracefully close the stream
        beast::error_code ec;
        stream.shutdown(ec);
        
        return res.body();
        
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] REST request failed: {}", e.what());
        throw;
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
            return OrderResponse{false, "Failed to parse JSON response"};
        }
        
        OrderResponse response;
        
        if (doc.HasMember("retCode") && doc["retCode"].GetInt() == 0) {
            response.success = true;
            response.message = doc.HasMember("retMsg") ? doc["retMsg"].GetString() : "Success";
            
            if (doc.HasMember("result") && doc["result"].IsObject()) {
                const auto& result = doc["result"];
                
                if (result.HasMember("orderId")) {
                    response.exchange_order_id = result["orderId"].GetString();
                }
                if (result.HasMember("orderLinkId")) {
                    response.client_order_id = result["orderLinkId"].GetString();
                }
                if (result.HasMember("orderStatus")) {
                    response.status = map_order_status(result["orderStatus"].GetString());
                }
            }
        } else {
            response.success = false;
            response.message = doc.HasMember("retMsg") ? doc["retMsg"].GetString() : "Unknown error";
        }
        
        return response;
        
    } catch (const std::exception& e) {
        return OrderResponse{false, std::string("Parse error: ") + e.what()};
    }
}

std::string BybitClient::map_order_status(const std::string& bybit_status) const {
    // Map Bybit status to our standard status
    if (bybit_status == "New") return "new";
    if (bybit_status == "PartiallyFilled") return "partially_filled";
    if (bybit_status == "Filled") return "filled";
    if (bybit_status == "Cancelled" || bybit_status == "PartiallyFilledCanceled") return "cancelled";
    if (bybit_status == "Rejected") return "rejected";
    return bybit_status;
}

void BybitClient::websocket_thread_func() {
    while (!should_stop_) {
        try {
            // Create WebSocket stream
            tcp::resolver resolver(*ioc_);
            ws_ = std::make_unique<websocket::stream<ssl::stream<tcp::socket>>>(*ioc_, *ssl_ctx_);
            
            // Set SNI hostname
            if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), ws_host_.c_str())) {
                throw beast::system_error(
                    beast::error_code(static_cast<int>(::ERR_get_error()),
                    net::error::get_ssl_category()),
                    "Failed to set SNI hostname");
            }
            
            // Connect TCP socket
            auto const results = resolver.resolve(ws_host_, ws_port_);
            boost::asio::connect(beast::get_lowest_layer(*ws_), results);
            
            // Perform SSL handshake
            ws_->next_layer().handshake(ssl::stream_base::client);
            
            // Perform WebSocket handshake
            ws_->handshake(ws_host_, ws_target_);
            
            ws_connected_ = true;
            spdlog::info("[BybitClient] WebSocket connected");
            
            // Send authentication
            if (!send_websocket_auth()) {
                spdlog::error("[BybitClient] WebSocket authentication failed");
                ws_connected_ = false;
                continue;
            }
            
            // Message reading loop
            while (!should_stop_ && ws_connected_) {
                beast::flat_buffer buffer;
                beast::error_code ec;
                
                // Read message with timeout
                ws_->read(buffer, ec);
                
                if (ec) {
                    if (ec != websocket::error::closed) {
                        spdlog::error("[BybitClient] WebSocket read error: {}", ec.message());
                    }
                    ws_connected_ = false;
                    break;
                }
                
                std::string message = beast::buffers_to_string(buffer.data());
                process_websocket_message(message);
            }
            
        } catch (const std::exception& e) {
            spdlog::error("[BybitClient] WebSocket error: {}", e.what());
            ws_connected_ = false;
        }
        
        if (!should_stop_) {
            spdlog::info("[BybitClient] Reconnecting WebSocket in 5 seconds...");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

bool BybitClient::send_websocket_auth() {
    try {
        std::string expires = std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch() + std::chrono::seconds(10)
            ).count()
        );
        
        std::string sign_payload = "GET/realtime" + expires;
        std::string signature = hmac_sha256(api_secret_, sign_payload);
        
        rapidjson::Document auth_msg;
        auth_msg.SetObject();
        auto& allocator = auth_msg.GetAllocator();
        
        auth_msg.AddMember("op", "auth", allocator);
        
        rapidjson::Value args(rapidjson::kArrayType);
        args.PushBack(rapidjson::Value(api_key_.c_str(), allocator), allocator);
        args.PushBack(rapidjson::Value(expires.c_str(), allocator), allocator);
        args.PushBack(rapidjson::Value(signature.c_str(), allocator), allocator);
        
        auth_msg.AddMember("args", args, allocator);
        
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        auth_msg.Accept(writer);
        
        ws_->write(net::buffer(buffer.GetString(), buffer.GetSize()));
        
        // Wait for auth response
        beast::flat_buffer response_buffer;
        ws_->read(response_buffer);
        std::string response = beast::buffers_to_string(response_buffer.data());
        
        rapidjson::Document response_doc;
        response_doc.Parse(response.c_str());
        
        if (response_doc.HasMember("success") && response_doc["success"].GetBool()) {
            spdlog::info("[BybitClient] WebSocket authenticated successfully");
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] WebSocket auth failed: {}", e.what());
        return false;
    }
}

bool BybitClient::send_websocket_subscribe(const std::vector<std::string>& topics) {
    try {
        rapidjson::Document sub_msg;
        sub_msg.SetObject();
        auto& allocator = sub_msg.GetAllocator();
        
        sub_msg.AddMember("op", "subscribe", allocator);
        
        rapidjson::Value args(rapidjson::kArrayType);
        for (const auto& topic : topics) {
            args.PushBack(rapidjson::Value(topic.c_str(), allocator), allocator);
        }
        
        sub_msg.AddMember("args", args, allocator);
        
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        sub_msg.Accept(writer);
        
        ws_->write(net::buffer(buffer.GetString(), buffer.GetSize()));
        
        spdlog::info("[BybitClient] Subscribed to topics: {}", buffer.GetString());
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] WebSocket subscribe failed: {}", e.what());
        return false;
    }
}

void BybitClient::process_websocket_message(const std::string& message) {
    try {
        rapidjson::Document doc;
        doc.Parse(message.c_str());
        
        if (doc.HasParseError()) {
            spdlog::error("[BybitClient] Failed to parse WebSocket message");
            return;
        }
        
        // Handle different message types
        if (doc.HasMember("topic")) {
            std::string topic = doc["topic"].GetString();
            
            if (topic == "order") {
                handle_order_update_message(doc);
            } else if (topic == "execution") {
                handle_execution_message(doc);
            }
        } else if (doc.HasMember("op")) {
            std::string op = doc["op"].GetString();
            if (op == "pong") {
                last_pong_time_ = std::chrono::steady_clock::now();
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] Error processing WebSocket message: {}", e.what());
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
            update.status = map_order_status(order_data["orderStatus"].GetString());
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
        FillData fill;
        
        if (exec_data.HasMember("orderLinkId")) {
            fill.client_order_id = exec_data["orderLinkId"].GetString();
        }
        if (exec_data.HasMember("orderId")) {
            fill.exchange_order_id = exec_data["orderId"].GetString();
        }
        if (exec_data.HasMember("execId")) {
            fill.exec_id = exec_data["execId"].GetString();
        }
        if (exec_data.HasMember("symbol")) {
            fill.symbol = exec_data["symbol"].GetString();
        }
        if (exec_data.HasMember("side")) {
            std::string side = exec_data["side"].GetString();
            fill.side = (side == "Buy") ? "buy" : "sell";
        }
        if (exec_data.HasMember("execPrice")) {
            fill.price = exec_data["execPrice"].GetString();
        }
        if (exec_data.HasMember("execQty")) {
            fill.quantity = exec_data["execQty"].GetString();
        }
        if (exec_data.HasMember("execFee")) {
            fill.fee = exec_data["execFee"].GetString();
        }
        if (exec_data.HasMember("feeRate")) {
            fill.fee_currency = "USDT";  // Bybit typically charges fees in quote currency
        }
        if (exec_data.HasMember("isMaker")) {
            fill.liquidity = exec_data["isMaker"].GetBool() ? "maker" : "taker";
        }
        if (exec_data.HasMember("execTime")) {
            fill.timestamp_ms = std::stoull(exec_data["execTime"].GetString());
        }
        
        if (fill_callback_) {
            fill_callback_(fill);
        }
    }
}

void BybitClient::send_websocket_ping() {
    try {
        rapidjson::Document ping_msg;
        ping_msg.SetObject();
        ping_msg.AddMember("op", "ping", ping_msg.GetAllocator());
        
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        ping_msg.Accept(writer);
        
        ws_->write(net::buffer(buffer.GetString(), buffer.GetSize()));
        last_ping_time_ = std::chrono::steady_clock::now();
        
    } catch (const std::exception& e) {
        spdlog::error("[BybitClient] Failed to send ping: {}", e.what());
    }
}

} // namespace latentspeed