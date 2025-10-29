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
    // Dump recent TX/RX to help isolate cause
    diag_dump_recent("heartbeat_stale");
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
        diag_push_rx("ch=" + channel + ", bytes=" + std::to_string(bytes) + ", t=" + std::to_string(now_ms()));
    } catch (...) {}
}

bool HlWsPostClient::enqueue_frame(OutboundFrame frame) {
    if (!connected_.load(std::memory_order_acquire)) {
        return false;
    }
    const char* frame_type_str = nullptr;
    switch (frame.type) {
        case FrameType::Post: frame_type_str = "post"; break;
        case FrameType::Subscribe: frame_type_str = "subscribe"; break;
        case FrameType::Ping: frame_type_str = "ping"; break;
    }
    {
        std::lock_guard<std::mutex> lk(outbound_mutex_);
        if (stop_writer_.load(std::memory_order_acquire)) {
            return false;
        }
        outbound_queue_.emplace_back(std::move(frame));
        try {
            spdlog::info("[HL-WS] enqueue frame type={} queue_size={}", frame_type_str, outbound_queue_.size());
        } catch (...) {}
    }
    outbound_cv_.notify_one();
    return true;
}

void HlWsPostClient::schedule_diag_ping_after_post(uint64_t post_id) {
    std::thread([this, post_id]() {
        try {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            if (stop_.load(std::memory_order_acquire)) return;
            if (!ensure_connected_locked()) return;
            OutboundFrame frame;
            frame.type = FrameType::Ping;
            frame.payload = "{\"method\":\"ping\"}";
            frame.meta_a = last_msg_ms_.load(std::memory_order_acquire);
            frame.meta_b = last_ping_ms_.load(std::memory_order_acquire);
            frame.meta_c = now_ms();
            frame.tag = std::string("post_id=") + std::to_string(post_id);
            enqueue_frame(std::move(frame));
        } catch (...) {}
    }).detach();
}

void HlWsPostClient::writer_loop() {
    while (true) {
        OutboundFrame frame;
        {
            std::unique_lock<std::mutex> lk(outbound_mutex_);
            outbound_cv_.wait(lk, [&]{
                return stop_writer_.load(std::memory_order_acquire) || !outbound_queue_.empty();
            });
            if (stop_writer_.load(std::memory_order_acquire) && outbound_queue_.empty()) {
                break;
            }
            frame = std::move(outbound_queue_.front());
            outbound_queue_.pop_front();
        }

        if (!ensure_connected_locked()) {
            if (frame.type == FrameType::Post) {
                std::shared_ptr<Pending> entry;
                {
                    std::lock_guard<std::mutex> lk(corr_mutex_);
                    auto it = pending_.find(frame.id);
                    if (it != pending_.end()) {
                        entry = it->second;
                        pending_.erase(it);
                    }
                }
                if (entry) {
                    std::lock_guard<std::mutex> lk(entry->m);
                    entry->timed_out = true;
                    entry->ready = true;
                    entry->response.clear();
                    entry->cv.notify_all();
                }
            }
            continue;
        }

        auto release_pending = [&](uint64_t id) {
            if (id == 0) return;
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
                std::lock_guard<std::mutex> lk(entry->m);
                entry->timed_out = true;
                entry->ready = true;
                entry->response.clear();
                entry->cv.notify_all();
            }
        };

        try {
            const std::string& payload = frame.payload;
            auto send_buffer = net::buffer(payload);
            auto write_start = std::chrono::steady_clock::now();
            // Note: text(true) is set once during connection setup to avoid race with concurrent read
            if (wss_) {
                wss_->write(send_buffer);
            } else if (ws_) {
                ws_->write(send_buffer);
            } else {
                throw std::runtime_error("no active websocket stream");
            }
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - write_start).count();

            if (frame.type == FrameType::Ping) {
                uint64_t diag_last_msg = frame.meta_a;
                uint64_t diag_prev_ping = frame.meta_b;
                uint64_t diag_now = frame.meta_c != 0 ? frame.meta_c : now_ms();
                try {
                    if (!frame.tag.empty()) {
                        spdlog::info("[HL-WS] ping sent ({})", frame.tag);
                    } else {
                        spdlog::info("[HL-WS] ping sent");
                    }
                    spdlog::info("[HL-WS] ping diagnostics last_msg_ms={} prev_ping_ms={} now_ms={}",
                                 diag_last_msg,
                                 diag_prev_ping,
                                 diag_now);
                    if (!frame.tag.empty()) {
                        diag_push_tx("ping t=" + std::to_string(diag_now) + " (" + frame.tag + ")");
                    } else {
                        diag_push_tx("ping t=" + std::to_string(diag_now));
                    }
                } catch (...) {}
            } else if (frame.type == FrameType::Subscribe) {
                try {
                    spdlog::info("[HL-WS] subscribe frame sent bytes={} elapsed_ms={}", payload.size(), elapsed);
                    diag_push_tx("sub bytes=" + std::to_string(payload.size()) + ", t=" + std::to_string(now_ms()));
                } catch (...) {}
            } else if (frame.type == FrameType::Post) {
                try {
                    spdlog::info("[HL-WS] post frame sent id={} bytes={} elapsed_ms={}", frame.id, payload.size(), elapsed);
                    diag_push_tx("post id=" + std::to_string(frame.id) + ", bytes=" + std::to_string(payload.size()) + ", t=" + std::to_string(now_ms()));
                } catch (...) {}
            }
        } catch (const std::exception& e) {
            try { spdlog::warn("[HL-WS] writer error: {}", e.what()); } catch (...) {}
            connected_.store(false, std::memory_order_release);
            stop_writer_.store(true, std::memory_order_release);
            release_pending(frame.id);
            diag_dump_recent("writer_error");
            break;
        } catch (...) {
            try { spdlog::warn("[HL-WS] writer error: unknown exception"); } catch (...) {}
            connected_.store(false, std::memory_order_release);
            stop_writer_.store(true, std::memory_order_release);
            release_pending(frame.id);
            diag_dump_recent("writer_error_unknown");
            break;
        }
    }

    // Flush any remaining frames (fail pending posts) before exit
    std::deque<OutboundFrame> leftovers;
    {
        std::lock_guard<std::mutex> lk(outbound_mutex_);
        leftovers.swap(outbound_queue_);
    }
    for (auto& frame : leftovers) {
        if (frame.type == FrameType::Post) {
            std::shared_ptr<Pending> entry;
            {
                std::lock_guard<std::mutex> lk(corr_mutex_);
                auto it = pending_.find(frame.id);
                if (it != pending_.end()) {
                    entry = it->second;
                    pending_.erase(it);
                }
            }
            if (entry) {
                std::lock_guard<std::mutex> lk(entry->m);
                entry->timed_out = true;
                entry->ready = true;
                entry->response.clear();
                entry->cv.notify_all();
            }
        }
    }
    try {
        spdlog::info("[HL-WS] writer_loop exit stop_writer={} connected={}",
                     stop_writer_.load(std::memory_order_acquire),
                     connected_.load(std::memory_order_acquire));
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
            // Set TCP keepalive and no-delay options
            try {
                auto& socket = beast::get_lowest_layer(*wss_);
                socket.set_option(net::socket_base::keep_alive(true));
                socket.set_option(tcp::no_delay(true));
            } catch (...) {}
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
                    // Disable automatic timeout/ping - we handle keepalive at application layer
                    wss_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
                    wss_->set_option(websocket::stream_base::timeout{
                        std::chrono::seconds(300),  // handshake timeout
                        std::chrono::seconds(0),    // idle timeout (disable auto ping)
                        false                        // keep-alive pings disabled
                    });
                    wss_->set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                        req.set(beast::http::field::user_agent, std::string("LatentSpeed-HL/1.0"));
                    }));
                    // Set text mode once for all outgoing messages (avoid race with concurrent read)
                    wss_->text(true);
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
            // Set TCP keepalive and no-delay options
            try {
                auto& socket = beast::get_lowest_layer(*ws_);
                socket.set_option(net::socket_base::keep_alive(true));
                socket.set_option(tcp::no_delay(true));
            } catch (...) {}
            std::thread hs_thr([&]() {
                try {
                    // Disable automatic timeout/ping - we handle keepalive at application layer
                    ws_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
                    ws_->set_option(websocket::stream_base::timeout{
                        std::chrono::seconds(300),  // handshake timeout
                        std::chrono::seconds(0),    // idle timeout (disable auto ping)
                        false                        // keep-alive pings disabled
                    });
                    ws_->set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                        req.set(beast::http::field::user_agent, std::string("LatentSpeed-HL/1.0"));
                    }));
                    // Set text mode once for all outgoing messages (avoid race with concurrent read)
                    ws_->text(true);
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
        stop_writer_.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(outbound_mutex_);
            outbound_queue_.clear();
        }
        last_rx_ = std::chrono::steady_clock::now();
        uint64_t now_ms_val = now_ms();
        last_msg_ms_.store(now_ms_val, std::memory_order_release);
        last_ping_ms_.store(0, std::memory_order_release);
        writer_thread_ = std::make_unique<std::thread>(&HlWsPostClient::writer_loop, this);
        rx_thread_ = std::make_unique<std::thread>(&HlWsPostClient::rx_loop, this);
        // Start heartbeat thread: check liveness frequently; ping near 50s; recycle if no server msg > 65s
        stop_hb_.store(false, std::memory_order_release);
        hb_thread_ = std::make_unique<std::thread>([this]() {
            constexpr uint64_t kPingIntervalMs = 20000;   // send ping if no server message ~20s
            constexpr uint64_t kStaleIntervalMs = 45000;  // consider dead if no server message >45s
            while (!stop_hb_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (stop_hb_.load(std::memory_order_acquire)) break;
                const uint64_t now = now_ms();
                const uint64_t last_msg = last_msg_ms_.load(std::memory_order_acquire);
                const uint64_t last_ping = last_ping_ms_.load(std::memory_order_acquire);
                if (!ensure_connected_locked()) continue;
                // stale detection aligned with HL 60s server close policy (give a little headroom)
                if (last_msg > 0 && now > last_msg && (now - last_msg) > kStaleIntervalMs) {
                    mark_heartbeat_stale(now, last_msg, last_ping);
                    continue;
                }
                // proactive ping if quiet approaches server timeout and we haven't pinged recently
                if (last_msg > 0 && now > last_msg && (now - last_msg) >= kPingIntervalMs) {
                    // avoid spamming ping; only once every ~kPingIntervalMs
                    if (!(last_ping > 0 && now > last_ping && (now - last_ping) < (kPingIntervalMs - 5000))) {
                        try { send_ping(); } catch (...) {}
                    }
                }
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
    stop_writer_.store(true, std::memory_order_release);
    outbound_cv_.notify_all();
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
    if (writer_thread_ && writer_thread_->joinable()) { writer_thread_->join(); }
    writer_thread_.reset();
    if (rx_thread_ && rx_thread_->joinable()) { rx_thread_->join(); }
    rx_thread_.reset();
    {
        std::lock_guard<std::mutex> lk(outbound_mutex_);
        outbound_queue_.clear();
    }
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
        const uint64_t now_ms_val = now_ms();
        const uint64_t prev_ping_ms = last_ping_ms_.load(std::memory_order_acquire);
        const uint64_t last_msg_ms = last_msg_ms_.load(std::memory_order_acquire);
        if (!connected_.load(std::memory_order_acquire)) {
            return;
        }
        OutboundFrame frame;
        frame.type = FrameType::Ping;
        frame.payload = "{\"method\":\"ping\"}";
        frame.meta_a = last_msg_ms;
        frame.meta_b = prev_ping_ms;
        frame.meta_c = now_ms_val;
        if (!enqueue_frame(std::move(frame))) {
            spdlog::warn("[HL-WS] ping enqueue failed: writer inactive");
            connected_.store(false, std::memory_order_release);
            return;
        }
        last_ping_ms_.store(now_ms_val, std::memory_order_release);
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
    // Prepare pending entry
    std::shared_ptr<Pending> pending = std::make_shared<Pending>();
    uint64_t id;
    {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        id = next_id_++;
    }
    size_t pending_count = 0;

    // Build wrapper JSON while the transmit mutex is held so we can send atomically.
    rapidjson::Document d(rapidjson::kObjectType);
    auto& alloc = d.GetAllocator();
    d.AddMember("method", rapidjson::Value("post", alloc), alloc);
    d.AddMember("id", rapidjson::Value(static_cast<uint64_t>(id)), alloc);
    rapidjson::Value req(rapidjson::kObjectType);
    req.AddMember("type", rapidjson::Value(type.c_str(), alloc), alloc);

    rapidjson::Document payload;
   payload.Parse(payload_json.c_str());
    if (payload.HasParseError()) {
        return std::nullopt;
    }
    req.AddMember("payload", payload, alloc);
    d.AddMember("request", req, alloc);
    rapidjson::StringBuffer sb; rapidjson::Writer<rapidjson::StringBuffer> wr(sb); d.Accept(wr);
    std::string frame_payload(sb.GetString(), sb.GetSize());

    {
        std::lock_guard<std::mutex> lk(corr_mutex_);
        pending_[id] = pending;
        pending_count = pending_.size();
    }
    try {
        spdlog::info("[HL-WS] post request id={} type={} payload_bytes={} timeout_ms={} pending_count={}",
                     id, type, payload_json.size(), timeout.count(), pending_count);
    } catch (...) {}

    OutboundFrame frame;
    frame.type = FrameType::Post;
    frame.id = id;
    frame.payload = std::move(frame_payload);
    if (!enqueue_frame(std::move(frame))) {
        {
            std::lock_guard<std::mutex> lk(corr_mutex_);
            pending_.erase(id);
        }
        spdlog::warn("[HL-WS] post enqueue failed id={} (writer inactive)", id);
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
    uint64_t last_heartbeat_log = 0;
    while (!stop_.load(std::memory_order_acquire)) {
        try {
            if (!ensure_connected_locked()) break;
            // Periodic heartbeat to detect if rx_loop is alive but blocked
            uint64_t now_val = now_ms();
            if (last_heartbeat_log == 0 || (now_val - last_heartbeat_log) >= 10000) {
                try {
                    spdlog::info("[HL-WS] rx_loop heartbeat: alive and waiting for next frame (last_msg_ms={}, buffer_size={})",
                                 last_msg_ms_.load(std::memory_order_acquire), buffer.size());
                } catch (...) {}
                last_heartbeat_log = now_val;
            }
            std::size_t n = 0;
            bool frame_is_text = true;
            bool frame_is_binary = false;
            try {
                spdlog::trace("[HL-WS] rx_loop: calling read() to wait for next frame");
            } catch (...) {}
            if (wss_) {
                n = wss_->read(buffer);
                try {
                    frame_is_text = wss_->got_text();
                    frame_is_binary = wss_->got_binary();
                } catch (...) {}
            }
            else if (ws_) {
                n = ws_->read(buffer);
                try {
                    frame_is_text = ws_->got_text();
                    frame_is_binary = ws_->got_binary();
                } catch (...) {}
            }
            else break;
            try {
                spdlog::trace("[HL-WS] rx_loop: read() returned n={} bytes", n);
            } catch (...) {}
            (void)n;
            last_rx_ = std::chrono::steady_clock::now();
            const uint64_t now_ms_val = now_ms();
            last_msg_ms_.store(now_ms_val, std::memory_order_release);

            // Log control frames (pong, close, etc.) explicitly for diagnostics
            if (!frame_is_text && !frame_is_binary) {
                try {
                    spdlog::info("[HL-WS] rx control frame: bytes={} (likely pong/close/ping) - last_msg_ms updated to {}",
                                 n, now_ms_val);
                } catch (...) {}
            }
            std::string msg = beast::buffers_to_string(buffer.cdata());
            buffer.consume(buffer.size());
            // Parse and route post response
            rapidjson::Document d; d.Parse(msg.c_str());
            if (d.HasParseError() || !d.IsObject()) {
                try {
                    spdlog::info("[HL-WS] rx non-object payload bytes={} text={} data={}", msg.size(), (frame_is_text?"yes":"no"), msg.substr(0, std::min<std::size_t>(512, msg.size())));
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
                            try { diag_push_rx("post id=" + std::to_string(id) + ", t=" + std::to_string(now_ms_val)); } catch (...) {}
                            // One-shot diagnostic ping ~3s after a successful post response (action type preferred)
                            try {
                                std::string resp_type;
                                if (d.HasMember("data") && d["data"].IsObject()) {
                                    const auto& dd = d["data"];
                                    if (dd.HasMember("response") && dd["response"].IsObject()) {
                                        const auto& r = dd["response"];
                                        if (r.HasMember("type") && r["type"].IsString()) resp_type = r["type"].GetString();
                                    }
                                }
                                if (resp_type.empty() || resp_type == std::string("action")) {
                                    schedule_diag_ping_after_post(id);
                                }
                            } catch (...) {}
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
                        try { diag_push_rx("pong t=" + std::to_string(now_ms_val)); } catch (...) {}
                    } else if (ch == "heartbeat") {
                        try { spdlog::info("[HL-WS] heartbeat channel payload: {}", msg); } catch (...) {}
                    } else if (ch != "orderUpdates" && ch != "userEvents" && ch != "userFills") {
                        try {
                            spdlog::info("[HL-WS] rx channel={} payload={}", ch, msg.substr(0, std::min<std::size_t>(512, msg.size())));
                            try { diag_push_rx("ch=" + ch + ", t=" + std::to_string(now_ms_val)); } catch (...) {}
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
            // Heartbeat is driven by a dedicated scheduler; no implicit RX-driven pings here.
        } catch (const beast::system_error& e) {
            try {
                spdlog::warn("[HL-WS] rx_loop websocket error: code={} message={}", e.code().value(), e.what());
            } catch (...) {}
            diag_dump_recent("rx_loop_error");
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
            diag_dump_recent("rx_loop_exception");
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
        OutboundFrame frame;
        frame.type = FrameType::Subscribe;
        frame.payload.assign(sb.GetString(), sb.GetSize());
        return enqueue_frame(std::move(frame));
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
        OutboundFrame frame;
        frame.type = FrameType::Subscribe;
        frame.payload.assign(sb.GetString(), sb.GetSize());
        return enqueue_frame(std::move(frame));
    } catch (const std::exception& e) {
        spdlog::error("[HL-WS] subscribe(error): {}", e.what());
        return false;
    }
}

} // namespace latentspeed::netws
