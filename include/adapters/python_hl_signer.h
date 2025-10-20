/**
 * @file python_hl_signer.h
 * @brief Python-backed Hyperliquid signer bridge (persistent subprocess, NDJSON over stdio).
 */

#pragma once

#include "adapters/hyperliquid_signer.h"
#include <string>
#include <optional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <condition_variable>
#include <memory>
#include <atomic>
#include <cstdint>

namespace latentspeed {

class PythonHyperliquidSigner final : public IHyperliquidSigner {
public:
    PythonHyperliquidSigner(const std::string& python_exe = "python3",
                            const std::string& script_path = "latentspeed/tools/hl_signer_bridge.py");
    ~PythonHyperliquidSigner() override;

    std::optional<HlSignature> sign_l1_action(const std::string& private_key_hex_lower,
                                              const std::string& action_json,
                                              const std::optional<std::string>& vault_address_lower,
                                              std::uint64_t nonce,
                                              const std::optional<std::uint64_t>& expires_after,
                                              bool is_mainnet) override;

private:
    bool ensure_started();
    void reader_loop();
    bool send_line(const std::string& line);

    std::string python_exe_;
    std::string script_path_;

    int child_pid_{-1};
    int fd_stdin_{-1};
    int fd_stdout_{-1};
    std::mutex io_mutex_;
    std::unique_ptr<std::thread> reader_thread_;
    std::atomic<bool> running_{false};

    struct Pending {
        std::optional<HlSignature> sig;
        std::string error;
        bool ready{false};
        std::condition_variable cv;
    };
    std::mutex corr_mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<Pending>> pending_;
    std::uint64_t next_id_{1};
};

} // namespace latentspeed
