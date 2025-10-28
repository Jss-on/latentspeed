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

uint64_t HlWsPostClient::now_ms() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

void HlWsPostClient::mark_heartbeat_stale(uint64_t now_ms_val, uint64_t last_msg_ms, uint64_t last_ping_ms) {
    uint64_t delta = (last_msg_ms > 0 && now_ms_val > last_msg_ms) ? (now_ms_val - last_msg_ms) : 0;
    try {
        spdlog::warn("[HL-WS] heartbeat stale: last server message {} ms ago (last_ping_ms={}, now_ms={}); marking disconnected",
                     delta, last_ping_ms, now_ms_val);
    } catch (...) {}
    connected_.store(false, std::memory_order_release);
    try {
        if (wss_) {
            beast::error_code ec;
            beast::get_lowest_layer(*wss_).cancel(ec);
        } else if (ws_) {
            beast::error_code ec;
            beast::get_lowest_layer(*ws_).cancel(ec);
        }
    } catch (...) {}
}

void HlWsPostClient::trace_rx(size_t bytes, const std::string& channel) {
    try {
        spdlog::info("[HL-WS] rx trace channel={} bytes={} last_msg_ms={} last_ping_ms={}",
                     channel,
                     bytes,
                     last_msg_ms_.load(std::memory_order_acquire),
                     last_ping_ms_.load(std::memory_order_acquire));
    } catch (...) {}
}

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
        spdlog::info("[HL-WS] connect attempt url={} host={} port={} target={} tls={}",
                     ws_url, host_, port_, target_, (use_tls_ ? "yes" : "no"));
    } catch (...) {}
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
        uint64_t now_ms_val = now_ms();
        last_msg_ms_.store(now_ms_val, std::memory_order_release);
        last_ping_ms_.store(0, std::memory_order_release);
        rx_thread_ = std::make_unique<std::thread>(&HlWsPostClient::rx_loop, this);
        // Start heartbeat thread to send app-level ping periodically regardless of RX activity
        stop_hb_.store(false, std::memory_order_release);
        hb_thread_ = std::make_unique<std::thread>([this]() {
            while (!stop_hb_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::seconds(50));
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
        constexpr uint64_t kPingResponseTimeoutMs = 20000; // allow up to 20s for a pong/data response
        const uint64_t now_ms_val = now_ms();
        const uint64_t prev_ping_ms = last_ping_ms_.load(std::memory_order_acquire);
        const uint64_t last_msg_ms = last_msg_ms_.load(std::memory_order_acquire);
        if (prev_ping_ms > 0 && last_msg_ms > 0 && last_msg_ms < prev_ping_ms &&
            now_ms_val > prev_ping_ms && (now_ms_val - prev_ping_ms) > kPingResponseTimeoutMs) {
            mark_heartbeat_stale(now_ms_val, last_msg_ms, prev_ping_ms);
            return;
        }
        std::lock_guard<std::mutex> lk(tx_mutex_);
        const std::string ping_payload = "{\"method\":\"ping\"}";
        if (wss_) {
            wss_->write(net::buffer(ping_payload));
        } else if (ws_) {
            ws_->write(net::buffer(ping_payload));
        } else {
            spdlog::warn("[HL-WS] ping send skipped: no active websocket stream");
            connected_.store(false, std::memory_order_release);
            return;
        }
        last_ping_ms_.store(now_ms_val, std::memory_order_release);
        try {
            spdlog::info("[HL-WS] ping sent");
            spdlog::info("[HL-WS] ping diagnostics last_msg_ms={} prev_ping_ms={} now_ms={}",
                         last_msg_ms_.load(std::memory_order_acquire),
                         prev_ping_ms,
                         now_ms_val);
        } catch (...) {}
    } catch (const std::exception& e) {
        spdlog::warn("[HL-WS] ping send failed: {}", e.what());
        connected_.store(false, std::memory_order_release);
    } catch (...) {
        spdlog::warn("[HL-WS] ping send failed: unknown exception");
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
    std::shared_ptr<Pending> pending = std::make_shared<Pending>();
    {
        std::lock_guard<std::mutex> lk(corr_mutex_);
        pending_[id] = pending;
    }

    try {
        spdlog::info("[HL-WS] post request id={} type={} payload_bytes={} timeout_ms={} pending_count={}",
                     id, type, payload_json.size(), timeout.count(), pending_.size());
    } catch (...) {}

    try {
        if (wss_) wss_->write(net::buffer(sb.GetString(), sb.GetSize()));
        else if (ws_) ws_->write(net::buffer(sb.GetString(), sb.GetSize()));
        else return std::nullopt;
    } catch (...) {
        connected_.store(false, std::memory_order_release);
        return std::nullopt;
    }

    // Wait for response
    std::shared_ptr<Pending> entry;
    {
        std::lock_guard<std::mutex> lk(corr_mutex_);
        auto it = pending_.find(id);
        if (it == pending_.end()) return std::nullopt;
        entry = it->second;
    }
    std::unique_lock<std::mutex> entry_lk(entry->m);
    if (!entry->cv.wait_for(entry_lk, timeout, [&]{ return entry->ready; })) {
        entry->timed_out = true;
        try {
            spdlog::warn("[HL-WS] post timeout id={} type={} timeout_ms={}", id, type, timeout.count());
        } catch (...) {}
        return std::nullopt;
    }
    std::string resp = std::move(entry->response);
    try {
        spdlog::info("[HL-WS] post response id={} type={} bytes={} pending_count={}", id, type, resp.size(), pending_.size());
    } catch (...) {}
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
            const uint64_t now_ms_val = now_ms();
            last_msg_ms_.store(now_ms_val, std::memory_order_release);
    std::string msg = beast::buffers_to_string(buffer.cdata());
    buffer.consume(buffer.size());
    // Parse and route post response
    rapidjson::Document d; d.Parse(msg.c_str());
    if (d.HasParseError() || !d.IsObject()) {
        try {
            spdlog::info("[HL-WS] rx non-object payload bytes={} data={}", msg.size(), msg.substr(0, std::min<std::size_t>(512, msg.size())));
        } catch (...) {}
        continue;
    }
    if (d.HasMember("channel") && d["channel"].IsString()) {
        std::string ch = d["channel"].GetString();
        trace_rx(msg.size(), ch);
        if (ch == "post" && d.HasMember("data") && d["data"].IsObject()) {
            auto& data = d["data"];
                    if (data.HasMember("id") && data["id"].IsUint64()) {
                        uint64_t id = data["id"].GetUint64();
                        std::string payload;
                        if (data.HasMember("response") && data["response"].IsObject()) {
                            rapidjson::StringBuffer sb; rapidjson::Writer<rapidjson::StringBuffer> wr(sb); data["response"].Accept(wr);
                            payload = sb.GetString();
                        }
                        std::shared_ptr<Pending> entry;
                        {
                            std::lock_guard<std::mutex> lk(corr_mutex_);
                            auto it = pending_.find(id);
                            if (it != pending_.end()) {
                                entry = it->second;
                                pending_.erase(it);
                            }
                        }
                        if (entry) {
                            bool timed_out = false;
                            {
                                std::lock_guard<std::mutex> lk(entry->m);
                                entry->response = std::move(payload);
                                entry->ready = true;
                                timed_out = entry->timed_out;
                            }
                            entry->cv.notify_all();
                            if (timed_out) {
                                try {
                                    spdlog::info("[HL-WS] post response id={} delivered after caller timeout", id);
                                } catch (...) {}
                            }
                        } else {
                            try {
                                spdlog::warn("[HL-WS] post response (id={}) arrived with no pending waiter", id);
                            } catch (...) {}
                        }
                    }
                } else {
                    if (ch == "pong") {
                        last_msg_ms_.store(now_ms_val, std::memory_order_release);
                        try { spdlog::info("[HL-WS] pong received"); } catch (...) {}
                    } else if (ch == "heartbeat") {
                        try { spdlog::info("[HL-WS] heartbeat channel payload: {}", msg); } catch (...) {}
                    } else if (ch != "orderUpdates" && ch != "userEvents" && ch != "userFills") {
                        try {
                            spdlog::info("[HL-WS] rx channel={} payload={}", ch, msg.substr(0, std::min<std::size_t>(512, msg.size())));
                        } catch (...) {}
                    }
                    if (handler_) {
                    handler_(ch, d);
                }
            }
            } else {
                try {
                    spdlog::info("[HL-WS] rx frame without channel bytes={} payload={}",
                                 msg.size(), msg.substr(0, std::min<std::size_t>(512, msg.size())));
                } catch (...) {}
            }
            // Heartbeat: if quiet > ~55s, send ping to stay under server timeout
            auto now = std::chrono::steady_clock::now();
            if (now - last_rx_ > std::chrono::seconds(55)) {
                send_ping();
                last_rx_ = now;
            }
        } catch (const beast::system_error& e) {
            try {
                spdlog::warn("[HL-WS] rx_loop websocket error: code={} message={}", e.code().value(), e.what());
            } catch (...) {}
            try {
                if (wss_ && wss_->is_open()) {
                    auto cr = wss_->reason();
                    spdlog::warn("[HL-WS] websocket close reason: code={} message={}", cr.code, std::string(cr.reason.begin(), cr.reason.end()));
                }
            } catch (...) {}
            connected_.store(false, std::memory_order_release);
            break;
        } catch (const std::exception& e) {
            try {
                spdlog::warn("[HL-WS] rx_loop error: {}", e.what());
            } catch (...) {}
            connected_.store(false, std::memory_order_release);
            break;
        }
    }
    try {
        spdlog::info("[HL-WS] rx_loop exit stop={} connected={} last_msg_ms={} last_ping_ms={}",
                     stop_.load(std::memory_order_acquire),
                     connected_.load(std::memory_order_acquire),
                     last_msg_ms_.load(std::memory_order_acquire),
                     last_ping_ms_.load(std::memory_order_acquire));
    } catch (...) {}
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
