/**
 * @file python_hl_signer.cpp
 */

#include "adapters/python_hl_signer.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <cstring>
#include <sstream>

namespace latentspeed {

static bool set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

PythonHyperliquidSigner::PythonHyperliquidSigner(const std::string& python_exe,
                                                 const std::string& script_path)
    : python_exe_(python_exe), script_path_(script_path) {}

PythonHyperliquidSigner::~PythonHyperliquidSigner() {
    running_.store(false, std::memory_order_release);
    if (reader_thread_ && reader_thread_->joinable()) reader_thread_->join();
    if (fd_stdin_ != -1) close(fd_stdin_);
    if (fd_stdout_ != -1) close(fd_stdout_);
    if (child_pid_ > 0) {
        kill(child_pid_, SIGTERM);
        int status = 0; (void)waitpid(child_pid_, &status, 0);
        child_pid_ = -1;
    }
}

bool PythonHyperliquidSigner::ensure_started() {
    if (child_pid_ > 0) return true;
    int inpipe[2];
    int outpipe[2];
    if (pipe(inpipe) != 0) return false;
    if (pipe(outpipe) != 0) { close(inpipe[0]); close(inpipe[1]); return false; }

    pid_t pid = fork();
    if (pid < 0) {
        close(inpipe[0]); close(inpipe[1]); close(outpipe[0]); close(outpipe[1]);
        return false;
    }
    if (pid == 0) {
        // Child
        dup2(inpipe[0], STDIN_FILENO);
        dup2(outpipe[1], STDOUT_FILENO);
        // Close FDs
        close(inpipe[0]); close(inpipe[1]);
        close(outpipe[0]); close(outpipe[1]);
        // Unbuffered Python
        execlp(python_exe_.c_str(), python_exe_.c_str(), "-u", script_path_.c_str(), (char*)nullptr);
        _exit(127);
    }
    // Parent
    child_pid_ = pid;
    fd_stdin_ = inpipe[1];
    fd_stdout_ = outpipe[0];
    close(inpipe[0]);
    close(outpipe[1]);
    set_nonblock(fd_stdout_);
    running_.store(true, std::memory_order_release);
    reader_thread_ = std::make_unique<std::thread>(&PythonHyperliquidSigner::reader_loop, this);
    return true;
}

void PythonHyperliquidSigner::reader_loop() {
    std::string buf;
    buf.reserve(4096);
    char tmp[1024];
    while (running_.load(std::memory_order_acquire)) {
        ssize_t n = read(fd_stdout_, tmp, sizeof(tmp));
        if (n > 0) {
            buf.append(tmp, tmp + n);
            // Process lines
            size_t pos = 0;
            while (true) {
                size_t nl = buf.find('\n', pos);
                if (nl == std::string::npos) {
                    // Keep remaining
                    buf.erase(0, pos);
                    break;
                }
                std::string line = buf.substr(pos, nl - pos);
                pos = nl + 1;
                if (line.empty()) continue;
                rapidjson::Document d; d.Parse(line.c_str());
                if (d.HasParseError() || !d.IsObject()) continue;
                uint64_t id = 0;
                if (d.HasMember("id") && d["id"].IsUint64()) id = d["id"].GetUint64();
                std::shared_ptr<Pending> p;
                {
                    std::lock_guard<std::mutex> lk(corr_mutex_);
                    auto it = pending_.find(id);
                    if (it != pending_.end()) p = it->second;
                }
                if (!p) continue;
                if (d.HasMember("result") && d["result"].IsObject()) {
                    auto& r = d["result"];
                    HlSignature sig;
                    if (r.HasMember("r") && r["r"].IsString()) sig.r = r["r"].GetString();
                    if (r.HasMember("s") && r["s"].IsString()) sig.s = r["s"].GetString();
                    if (r.HasMember("v") && r["v"].IsInt()) sig.v = std::to_string(r["v"].GetInt());
                    {
                        std::lock_guard<std::mutex> lk(corr_mutex_);
                        p->sig = sig; p->ready = true; p->cv.notify_all();
                        pending_.erase(id);
                    }
                } else if (d.HasMember("error") && d["error"].IsObject()) {
                    std::string msg;
                    auto& e = d["error"];
                    if (e.HasMember("message") && e["message"].IsString()) msg = e["message"].GetString();
                    std::lock_guard<std::mutex> lk(corr_mutex_);
                    p->error = msg; p->ready = true; p->cv.notify_all();
                    pending_.erase(id);
                }
            }
        } else {
            // Sleep briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

bool PythonHyperliquidSigner::send_line(const std::string& line) {
    const char* data = line.c_str();
    size_t len = line.size();
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd_stdin_, data + off, len - off);
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

std::optional<HlSignature> PythonHyperliquidSigner::sign_l1_action(const std::string& private_key_hex_lower,
                                                                   const std::string& action_json,
                                                                   const std::optional<std::string>& vault_address_lower,
                                                                   std::uint64_t nonce,
                                                                   const std::optional<std::uint64_t>& expires_after,
                                                                   bool is_mainnet) {
    if (!ensure_started()) return std::nullopt;
    uint64_t id;
    {
        std::lock_guard<std::mutex> lk(corr_mutex_);
        id = next_id_++;
    }
    rapidjson::Document params(rapidjson::kObjectType);
    auto& alloc = params.GetAllocator();
    params.AddMember("privateKey", rapidjson::Value(private_key_hex_lower.c_str(), alloc), alloc);
    // Action json is string; send as string to avoid reordering keys
    params.AddMember("action", rapidjson::Value(action_json.c_str(), alloc), alloc);
    params.AddMember("nonce", rapidjson::Value(static_cast<uint64_t>(nonce)), alloc);
    if (vault_address_lower.has_value())
        params.AddMember("vaultAddress", rapidjson::Value(vault_address_lower->c_str(), alloc), alloc);
    else
        params.AddMember("vaultAddress", rapidjson::Value(rapidjson::kNullType), alloc);
    if (expires_after.has_value())
        params.AddMember("expiresAfter", rapidjson::Value(static_cast<uint64_t>(*expires_after)), alloc);
    else
        params.AddMember("expiresAfter", rapidjson::Value(rapidjson::kNullType), alloc);
    params.AddMember("isMainnet", rapidjson::Value(is_mainnet), alloc);

    rapidjson::Document msg(rapidjson::kObjectType);
    msg.AddMember("id", rapidjson::Value(static_cast<uint64_t>(id)), msg.GetAllocator());
    msg.AddMember("method", rapidjson::Value("sign_l1", msg.GetAllocator()), msg.GetAllocator());
    msg.AddMember("params", params, msg.GetAllocator());
    rapidjson::StringBuffer sb; rapidjson::Writer<rapidjson::StringBuffer> wr(sb); msg.Accept(wr);
    std::string line = sb.GetString();
    line.push_back('\n');

    auto pending = std::make_shared<Pending>();
    {
        std::lock_guard<std::mutex> lk(corr_mutex_);
        pending_[id] = pending;
    }
    if (!send_line(line)) {
        std::lock_guard<std::mutex> lk(corr_mutex_);
        pending_.erase(id);
        return std::nullopt;
    }

    std::unique_lock<std::mutex> lk(corr_mutex_);
    if (!pending->cv.wait_for(lk, std::chrono::milliseconds(2000), [&]{ return pending->ready; })) {
        pending_.erase(id);
        return std::nullopt;
    }
    if (!pending->sig.has_value()) return std::nullopt;
    return pending->sig;
}

} // namespace latentspeed
