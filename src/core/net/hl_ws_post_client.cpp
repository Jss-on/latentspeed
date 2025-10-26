/**
 * @file hl_ws_post_client.cpp
 */

#include "core/net/hl_ws_post_client.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <spdlog/spdlog.h>

namespace latentspeed::netws {

HlWsPostClient::HlWsPostClient() {}
HlWsPostClient::~HlWsPostClient() { close(); }

void HlWsPostClient::parse_ws_url(const std::string& url, std::string& host, std::string& port, std::string& target, bool& tls) {
    tls = true; host.clear(); port = "443"; target = "/ws";
    std::string scheme = "wss";
    auto pos = url.find("://");
    std::string rest = url;
    if (pos != std::string::npos) { scheme = url.substr(0, pos); rest = url.substr(pos + 3); }
    tls = (scheme == "wss");
    auto slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    if (slash != std::string::npos) target = rest.substr(slash);
    auto colon = hostport.find(':');
    if (colon == std::string::npos) { host = hostport; port = tls ? "443" : "80"; }
    else { host = hostport.substr(0, colon); port = hostport.substr(colon + 1); }
}

bool HlWsPostClient::connect(const std::string& ws_url) {
    close();
    ioc_ = std::make_unique<net::io_context>();
    resolver_ = std::make_unique<tcp::resolver>(*ioc_);

    parse_ws_url(ws_url, host_, port_, target_, use_tls_);
    try {
        auto const results = resolver_->resolve(host_, port_);
        // Establish TCP & prepare streams
        bool ok_handshake = false;
        std::atomic<bool> done{false};
        if (use_tls_) {
            ssl_ctx_ = std::make_unique<ssl::context>(ssl::context::tls_client);
            ssl_ctx_->set_default_verify_paths();
            wss_ = std::make_unique<websocket::stream<ssl::stream<tcp::socket>>>(*ioc_, *ssl_ctx_);
            // Connect TCP then set SNI and perform SSL handshake
            net::connect(wss_->next_layer().next_layer(), results.begin(), results.end());
            // Handshake work in a helper thread with a hard deadline
            std::thread hs_thr([&]() {
                try {
                    // SNI
                    if(!::SSL_set_tlsext_host_name(wss_->next_layer().native_handle(), host_.c_str())) {
                        beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
                        throw beast::system_error{ec};
                    }
                    // Deadline enforcement handled by outer watchdog + cancel/close
                    wss_->next_layer().handshake(ssl::stream_base::client);
                    // WS handshake
                    wss_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
                    wss_->set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                        req.set(beast::http::field::user_agent, std::string("LatentSpeed-HL/1.0"));
                    }));
                    // Handshake watchdog will abort via cancel/close on timeout
                    wss_->handshake(host_, target_);
                    ok_handshake = true;
                } catch (...) {
                    ok_handshake = false;
                }
                done.store(true, std::memory_order_release);
            });
            // Wait up to 8s total; abort on timeout
            for (int i = 0; i < 80 && !done.load(std::memory_order_acquire); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!done.load(std::memory_order_acquire)) {
                spdlog::warn("[HL-WS] connect timeout during handshake (wss) host={} target={}", host_, target_);
                try { beast::error_code ec; beast::get_lowest_layer(*wss_).cancel(ec); beast::get_lowest_layer(*wss_).close(ec); } catch (...) {}
            }
            if (hs_thr.joinable()) hs_thr.join();
        } else {
            ws_ = std::make_unique<websocket::stream<tcp::socket>>(*ioc_);
            net::connect(ws_->next_layer(), results.begin(), results.end());
            std::thread hs_thr([&]() {
                try {
                    ws_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
                    ws_->set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                        req.set(beast::http::field::user_agent, std::string("LatentSpeed-HL/1.0"));
                    }));
                    // Handshake watchdog will abort via cancel/close on timeout
                    ws_->handshake(host_, target_);
                    ok_handshake = true;
                } catch (...) { ok_handshake = false; }
                done.store(true, std::memory_order_release);
            });
            for (int i = 0; i < 80 && !done.load(std::memory_order_acquire); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!done.load(std::memory_order_acquire)) {
                spdlog::warn("[HL-WS] connect timeout during handshake (ws) host={} target={}", host_, target_);
                try { beast::error_code ec; beast::get_lowest_layer(*ws_).cancel(ec); beast::get_lowest_layer(*ws_).close(ec); } catch (...) {}
            }
            if (hs_thr.joinable()) hs_thr.join();
        }
        if (!ok_handshake) { close(); return false; }
        connected_.store(true, std::memory_order_release);
        try { spdlog::info("[HL-WS] ws connected host={} target={} tls={} ", host_, target_, (use_tls_?"yes":"no")); } catch (...) {}
        stop_.store(false, std::memory_order_release);
        last_rx_ = std::chrono::steady_clock::now();
        rx_thread_ = std::make_unique<std::thread>(&HlWsPostClient::rx_loop, this);
        // Start heartbeat thread to send app-level ping periodically regardless of RX activity
        stop_hb_.store(false, std::memory_order_release);
        hb_thread_ = std::make_unique<std::thread>([this]() {
            using namespace std::chrono_literals;
            while (!stop_hb_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(10s);
                if (stop_hb_.load(std::memory_order_acquire)) break;
                if (!ensure_connected_locked()) continue;
                try {
                    send_ping();
                } catch (...) {}
            }
        });
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[HL-WS] connect failed (host={}, tls={}): {}", host_, use_tls_, e.what());
        close();
        return false;
    }
}

void HlWsPostClient::close() {
    stop_.store(true, std::memory_order_release);
    connected_.store(false, std::memory_order_release);
    // Stop heartbeat first
    stop_hb_.store(true, std::memory_order_release);
    if (hb_thread_ && hb_thread_->joinable()) hb_thread_->join();
    hb_thread_.reset();
    // Tear down transport aggressively to unblock any blocking reads in rx thread.
    // Avoid calling websocket::close() here (not thread-safe against concurrent read).
    try {
        if (wss_) {
            beast::error_code ec;
            beast::get_lowest_layer(*wss_).cancel(ec);
            beast::get_lowest_layer(*wss_).close(ec);
        }
    } catch (...) {}
    try {
        if (ws_) {
            beast::error_code ec;
            beast::get_lowest_layer(*ws_).cancel(ec);
            beast::get_lowest_layer(*ws_).close(ec);
        }
    } catch (...) {}
    if (rx_thread_ && rx_thread_->joinable()) { rx_thread_->join(); }
    rx_thread_.reset();
    wss_.reset();
    ws_.reset();
    ssl_ctx_.reset();
    resolver_.reset();
    ioc_.reset();
    // Clear pending to avoid deadlocks
    std::lock_guard<std::mutex> lk(corr_mutex_);
    for (auto& kv : pending_) { kv.second->ready = true; kv.second->cv.notify_all(); }
    pending_.clear();
}

bool HlWsPostClient::ensure_connected_locked() {
    return connected_.load(std::memory_order_acquire);
}

void HlWsPostClient::send_ping() {
    try {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        if (wss_) wss_->write(net::buffer(std::string("{\"method\":\"ping\"}")));
        else if (ws_) ws_->write(net::buffer(std::string("{\"method\":\"ping\"}")));
    } catch (...) {
        connected_.store(false, std::memory_order_release);
    }
}

std::optional<std::string> HlWsPostClient::post(const std::string& type,
                                                const std::string& payload_json,
                                                std::chrono::milliseconds timeout) {
    if (!ensure_connected_locked()) return std::nullopt;
    uint64_t id;
    {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        id = next_id_++;
    }

    // Build wrapper JSON
    rapidjson::Document d(rapidjson::kObjectType);
    auto& alloc = d.GetAllocator();
    d.AddMember("method", rapidjson::Value("post", alloc), alloc);
    d.AddMember("id", rapidjson::Value(static_cast<uint64_t>(id)), alloc);
    rapidjson::Value req(rapidjson::kObjectType);
    req.AddMember("type", rapidjson::Value(type.c_str(), alloc), alloc);

    rapidjson::Document payload;
    payload.Parse(payload_json.c_str());
    if (payload.HasParseError()) return std::nullopt;
    req.AddMember("payload", payload, alloc);
    d.AddMember("request", req, alloc);
    rapidjson::StringBuffer sb; rapidjson::Writer<rapidjson::StringBuffer> wr(sb); d.Accept(wr);

    // Prepare pending entry
    auto pending = std::make_shared<Pending>();
    {
        std::lock_guard<std::mutex> lk(corr_mutex_);
        pending_[id] = pending;
    }

    try {
        if (wss_) wss_->write(net::buffer(sb.GetString(), sb.GetSize()));
        else if (ws_) ws_->write(net::buffer(sb.GetString(), sb.GetSize()));
        else return std::nullopt;
    } catch (...) {
        connected_.store(false, std::memory_order_release);
        return std::nullopt;
    }

    // Wait for response
    std::unique_lock<std::mutex> lk(corr_mutex_);
    auto it = pending_.find(id);
    if (it == pending_.end()) return std::nullopt;
    auto& entry = it->second;
    if (!entry->cv.wait_for(lk, timeout, [&]{ return entry->ready; })) {
        pending_.erase(it);
        return std::nullopt;
    }
    std::string resp = std::move(entry->response);
    pending_.erase(it);
    return resp;
}

void HlWsPostClient::rx_loop() {
    beast::flat_buffer buffer;
    while (!stop_.load(std::memory_order_acquire)) {
        try {
            if (!ensure_connected_locked()) break;
            std::size_t n = 0;
            if (wss_) n = wss_->read(buffer);
            else if (ws_) n = ws_->read(buffer);
            else break;
            (void)n;
            last_rx_ = std::chrono::steady_clock::now();
            std::string msg = beast::buffers_to_string(buffer.cdata());
            buffer.consume(buffer.size());
            // Parse and route post response
            rapidjson::Document d; d.Parse(msg.c_str());
            if (d.HasParseError() || !d.IsObject()) continue;
            if (d.HasMember("channel") && d["channel"].IsString()) {
                std::string ch = d["channel"].GetString();
                // Treat pong as a liveness event and forward to handler
                // so adapters can update their last_private_event_ms_.
                if (ch == "post" && d.HasMember("data") && d["data"].IsObject()) {
                    auto& data = d["data"];
                    if (data.HasMember("id") && data["id"].IsUint64()) {
                        uint64_t id = data["id"].GetUint64();
                        std::string payload;
                        if (data.HasMember("response") && data["response"].IsObject()) {
                            rapidjson::StringBuffer sb; rapidjson::Writer<rapidjson::StringBuffer> wr(sb); data["response"].Accept(wr);
                            payload = sb.GetString();
                        }
                        std::lock_guard<std::mutex> lk(corr_mutex_);
                        auto it = pending_.find(id);
                        if (it != pending_.end()) {
                            it->second->response = std::move(payload);
                            it->second->ready = true;
                            it->second->cv.notify_all();
                        }
                    }
                } else if (handler_) {
                    handler_(ch, d);
                }
            }
            // Heartbeat: if quiet > ~12s, send ping (keeps private WS liveness under resubscribe threshold)
            auto now = std::chrono::steady_clock::now();
            if (now - last_rx_ > std::chrono::seconds(12)) {
                send_ping();
                last_rx_ = now;
            }
        } catch (const std::exception& e) {
            spdlog::warn("[HL-WS] rx_loop error: {}", e.what());
            connected_.store(false, std::memory_order_release);
            break;
        }
    }
}

bool HlWsPostClient::subscribe(const std::string& type,
                               const std::vector<std::pair<std::string,std::string>>& kv_fields) {
    try {
        rapidjson::Document d(rapidjson::kObjectType);
        auto& alloc = d.GetAllocator();
        d.AddMember("method", rapidjson::Value("subscribe", alloc), alloc);
        rapidjson::Value sub(rapidjson::kObjectType);
        sub.AddMember("type", rapidjson::Value(type.c_str(), alloc), alloc);
        for (const auto& kv : kv_fields) {
            sub.AddMember(rapidjson::Value(kv.first.c_str(), alloc), rapidjson::Value(kv.second.c_str(), alloc), alloc);
        }
        d.AddMember("subscription", sub, alloc);
        rapidjson::StringBuffer sb; rapidjson::Writer<rapidjson::StringBuffer> wr(sb); d.Accept(wr);
        if (wss_) wss_->write(net::buffer(sb.GetString(), sb.GetSize()));
        else if (ws_) ws_->write(net::buffer(sb.GetString(), sb.GetSize()));
        else return false;
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[HL-WS] subscribe error: {}", e.what());
        return false;
    }
}

bool HlWsPostClient::subscribe_with_bool(const std::string& type,
                                         const std::vector<std::pair<std::string,std::string>>& kv_fields,
                                         const std::vector<std::pair<std::string,bool>>& bool_fields) {
    try {
        rapidjson::Document d(rapidjson::kObjectType);
        auto& alloc = d.GetAllocator();
        d.AddMember("method", rapidjson::Value("subscribe", alloc), alloc);
        rapidjson::Value sub(rapidjson::kObjectType);
        sub.AddMember("type", rapidjson::Value(type.c_str(), alloc), alloc);
        for (const auto& kv : kv_fields) {
            sub.AddMember(rapidjson::Value(kv.first.c_str(), alloc), rapidjson::Value(kv.second.c_str(), alloc), alloc);
        }
        for (const auto& bv : bool_fields) {
            sub.AddMember(rapidjson::Value(bv.first.c_str(), alloc), rapidjson::Value(bv.second), alloc);
        }
        d.AddMember("subscription", sub, alloc);
        rapidjson::StringBuffer sb; rapidjson::Writer<rapidjson::StringBuffer> wr(sb); d.Accept(wr);
        if (wss_) wss_->write(net::buffer(sb.GetString(), sb.GetSize()));
        else if (ws_) ws_->write(net::buffer(sb.GetString(), sb.GetSize()));
        else return false;
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[HL-WS] subscribe(error): {}", e.what());
        return false;
    }
}

} // namespace latentspeed::netws
