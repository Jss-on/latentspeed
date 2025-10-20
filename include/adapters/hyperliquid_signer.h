/**
 * @file hyperliquid_signer.h
 * @brief Interface and scaffold for Hyperliquid user-signed action signatures.
 */

#pragma once

#include <string>
#include <optional>
#include <cstdint>

namespace latentspeed {

struct HlSignature {
    std::string r; // hex, lowercase, 0x-prefixed
    std::string s; // hex, lowercase, 0x-prefixed
    std::string v; // hex or decimal string as required by API (we use decimal string)
};

class IHyperliquidSigner {
public:
    virtual ~IHyperliquidSigner() = default;

    // L1 action signing (preferred): signs the action envelope using the official scheme.
    // action_json must be a JSON object string with stable key ordering in construction.
    virtual std::optional<HlSignature> sign_l1_action(const std::string& private_key_hex_lower,
                                                      const std::string& action_json,
                                                      const std::optional<std::string>& vault_address_lower,
                                                      std::uint64_t nonce,
                                                      const std::optional<std::uint64_t>& expires_after,
                                                      bool is_mainnet) = 0;
};

// Stub signer to allow compilation until Python/C++ signer is integrated.
class StubHyperliquidSigner final : public IHyperliquidSigner {
public:
    std::optional<HlSignature> sign_l1_action(const std::string&, const std::string&,
                                              const std::optional<std::string>&,
                                              uint64_t,
                                              const std::optional<uint64_t>&,
                                              bool) override {
        return std::nullopt;
    }
};

} // namespace latentspeed
