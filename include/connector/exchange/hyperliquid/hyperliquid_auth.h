/**
 * @file hyperliquid_auth.h
 * @brief Hyperliquid authentication and signing
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include "adapters/python_hl_signer.h"

namespace latentspeed::connector::hyperliquid {

/**
 * @class HyperliquidAuth
 * @brief Hyperliquid EIP-712 signing for orders
 * 
 * Implements Hyperliquid's authentication scheme:
 * 1. Action hashing with msgpack
 * 2. Phantom agent construction
 * 3. EIP-712 typed data signing
 * 
 * NOTE: This is a PLACEHOLDER implementation. Full implementation requires:
 * - libsecp256k1 for ECDSA signing
 * - keccak256 for Ethereum hashing
 * - msgpack for action hashing
 * 
 * For production, use the Python Hyperliquid SDK or implement full crypto stack.
 */
class HyperliquidAuth {
public:
    /**
     * @brief Constructor
     * @param api_key Wallet address (0x...) or vault address
     * @param api_secret Private key (hex string without 0x prefix)
     * @param use_vault True if using vault address
     */
    HyperliquidAuth(
        const std::string& api_key,
        const std::string& api_secret,
        bool use_vault = false
    );
    
    // ========================================================================
    // PUBLIC API
    // ========================================================================
    
    /**
     * @brief Sign an order action
     * @param action Order action JSON (Hyperliquid format)
     * @param nonce Nonce for replay protection
     * @param is_mainnet True for mainnet, false for testnet
     * @return Signed action with signature
     * 
     * Example action:
     * {
     *   "type": "order",
     *   "orders": [{
     *     "a": 0,            // Asset index
     *     "b": true,         // is_buy
     *     "p": "50000",      // Price
     *     "s": "0.01",       // Size
     *     "r": false,        // reduce_only
     *     "t": {...}         // Order type
     *   }],
     *   "grouping": "na"
     * }
     */
    nlohmann::json sign_l1_action(
        const nlohmann::json& action,
        uint64_t nonce,
        bool is_mainnet = true
    );
    
    /**
     * @brief Sign an order action with auto-generated nonce
     * @param action Order action JSON
     * @param is_mainnet True for mainnet, false for testnet
     * @return Signed action with signature
     */
    nlohmann::json sign_l1_action(
        const nlohmann::json& action,
        bool is_mainnet = true
    );
    
    /**
     * @brief Sign a cancel action
     * @param cancel_action Cancel action JSON
     * @param nonce Nonce
     * @param is_mainnet True for mainnet
     * @return Signed cancel with signature
     */
    nlohmann::json sign_cancel_action(
        const nlohmann::json& cancel_action,
        uint64_t nonce,
        bool is_mainnet = true
    );
    
    /**
     * @brief Get wallet address
     */
    std::string get_address() const { return api_key_; }
    
    /**
     * @brief Check if using vault
     */
    bool is_vault() const { return use_vault_; }

private:
    std::string api_key_;       ///< Wallet/vault address
    std::string api_secret_;    ///< Private key (hex)
    bool use_vault_;            ///< Whether using vault
    std::unique_ptr<PythonHyperliquidSigner> signer_;  ///< Python-backed signer
    
    // ========================================================================
    // EIP-712 SIGNING HELPERS
    // ========================================================================
    
    /**
     * @brief Compute action hash using msgpack
     * @param action Action JSON
     * @param vault_address Optional vault address
     * @param nonce Nonce
     * @return 32-byte hash
     * 
     * Process:
     * 1. Serialize action to msgpack
     * 2. Concatenate: action_bytes + vault_bytes + nonce_bytes
     * 3. Keccak256 hash
     */
    std::vector<uint8_t> action_hash(
        const nlohmann::json& action,
        const std::optional<std::string>& vault_address,
        uint64_t nonce
    );
    
    /**
     * @brief Construct phantom agent for EIP-712
     * @param hash Action hash
     * @param is_mainnet Mainnet flag
     * @return Phantom agent JSON
     * 
     * Format:
     * {
     *   "source": "a",  // Agent type
     *   "connectionId": <hash_as_bytes>
     * }
     */
    nlohmann::json construct_phantom_agent(
        const std::vector<uint8_t>& hash,
        bool is_mainnet
    );
    
    /**
     * @brief Sign EIP-712 typed data
     * @param typed_data EIP-712 structured data
     * @return Signature JSON with r, s, v
     * 
     * typed_data format:
     * {
     *   "domain": {...},
     *   "types": {...},
     *   "primaryType": "...",
     *   "message": {...}
     * }
     */
    nlohmann::json sign_inner(const nlohmann::json& typed_data);
    
    /**
     * @brief Convert Ethereum address to bytes
     * @param address Address string (0x...)
     * @return 20-byte array
     */
    std::vector<uint8_t> address_to_bytes(const std::string& address);
    
    /**
     * @brief Convert uint64 to big-endian bytes
     */
    std::vector<uint8_t> uint64_to_bytes(uint64_t value);
    
    /**
     * @brief Keccak256 hash (placeholder)
     * @param data Input data
     * @return 32-byte hash
     * 
     * NOTE: Placeholder - requires keccak implementation
     */
    std::vector<uint8_t> keccak256(const std::vector<uint8_t>& data);
    
    /**
     * @brief ECDSA sign with secp256k1 (placeholder)
     * @param message_hash 32-byte hash to sign
     * @return Signature (r, s, v)
     * 
     * NOTE: Placeholder - requires libsecp256k1
     */
    nlohmann::json ecdsa_sign(const std::vector<uint8_t>& message_hash);
    
    /**
     * @brief Serialize to msgpack (placeholder)
     * @param data JSON data
     * @return Msgpack bytes
     * 
     * NOTE: Placeholder - requires msgpack library
     */
    std::vector<uint8_t> serialize_msgpack(const nlohmann::json& data);
};

/**
 * @brief Exception thrown by HyperliquidAuth
 */
class HyperliquidAuthException : public std::runtime_error {
public:
    explicit HyperliquidAuthException(const std::string& message)
        : std::runtime_error("HyperliquidAuth: " + message) {}
};

} // namespace latentspeed::connector::hyperliquid
