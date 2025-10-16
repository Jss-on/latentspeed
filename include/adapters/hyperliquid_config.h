/**
 * @file hyperliquid_config.h
 * @brief Minimal config scaffold and defaults for Hyperliquid adapter (Phase 3 - M1).
 */

#pragma once

#include <string>

namespace latentspeed {

struct HyperliquidConfig {
    // Base endpoints
    std::string rest_base;   // e.g., https://api.hyperliquid.xyz or -testnet
    std::string ws_url;      // e.g., wss://api.hyperliquid.xyz/ws or -testnet

    // Capabilities (intent; actual wiring in later milestones)
    bool supports_ws_post{false};
    bool supports_private_ws{false};
    bool supports_modify_by_cloid{true};
    bool supports_noop{true};

    // Network
    bool testnet{false};

    static HyperliquidConfig for_network(bool testnet) {
        if (testnet) {
            return HyperliquidConfig{
                .rest_base = "https://api.hyperliquid-testnet.xyz",
                .ws_url = "wss://api.hyperliquid-testnet.xyz/ws",
                .supports_ws_post = true,
                .supports_private_ws = true,
                .supports_modify_by_cloid = true,
                .supports_noop = true,
                .testnet = true,
            };
        }
        return HyperliquidConfig{
            .rest_base = "https://api.hyperliquid.xyz",
            .ws_url = "wss://api.hyperliquid.xyz/ws",
            .supports_ws_post = true,
            .supports_private_ws = true,
            .supports_modify_by_cloid = true,
            .supports_noop = true,
            .testnet = false,
        };
    }
};

} // namespace latentspeed

