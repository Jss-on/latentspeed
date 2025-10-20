# Phase 4: Exchange-Specific Auth Modules

**Duration**: Week 3  
**Priority**: 🟡 Medium  
**Dependencies**: Phase 1 (ConnectorBase)

## Objectives

1. Create dedicated auth classes per exchange
2. Implement Hyperliquid EIP-712 signature generation
3. Implement dYdX v4 Cosmos SDK transaction signing
4. Isolate exchange-specific conversion utilities

---

## 1. Hyperliquid Auth Module

Based on actual Hummingbot implementation at:
`sub/hummingbot/hummingbot/connector/derivative/hyperliquid_perpetual/hyperliquid_perpetual_auth.py`

**File**: `include/connector/hyperliquid/hyperliquid_auth.h`

```cpp
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace latentspeed::connector {

/**
 * @class HyperliquidAuth
 * @brief Hyperliquid authentication using EIP-712 signatures
 * 
 * Implements the exact signature scheme from Hummingbot:
 * 1. msgpack action hashing
 * 2. Phantom agent construction
 * 3. EIP-712 typed data signing
 * 4. Vault mode support
 */
class HyperliquidAuth {
public:
    /**
     * @param api_key Wallet address (or vault address if use_vault=true)
     * @param api_secret Private key (hex string, with or without 0x prefix)
     * @param use_vault If true, api_key is vault address and trades are attributed to vault
     */
    HyperliquidAuth(
        const std::string& api_key,
        const std::string& api_secret,
        bool use_vault
    );

    // ========================================================================
    // REQUEST SIGNING (PUBLIC API)
    // ========================================================================
    
    /**
     * @brief Add authentication to order/cancel/leverage request
     * @param params Request parameters (will be modified in-place)
     * @param base_url Exchange URL (to determine mainnet vs testnet)
     * @return Signed request payload
     */
    nlohmann::json add_auth_to_params(
        const nlohmann::json& params,
        const std::string& base_url
    );

    // ========================================================================
    // SIGNATURE GENERATION (HUMMINGBOT PATTERN)
    // ========================================================================
    
    /**
     * @brief Sign L1 action (Hummingbot's sign_l1_action)
     * @param action Order/cancel/leverage action object
     * @param vault_address Optional vault address
     * @param nonce Timestamp in milliseconds
     * @param is_mainnet True for mainnet, false for testnet
     * @return Signature object {r, s, v}
     */
    nlohmann::json sign_l1_action(
        const nlohmann::json& action,
        const std::optional<std::string>& vault_address,
        uint64_t nonce,
        bool is_mainnet
    );

private:
    std::string api_key_;      // Wallet address or vault address
    std::string api_secret_;   // Private key
    bool use_vault_;
    
    // EIP-712 helpers (from Hummingbot)
    std::vector<uint8_t> action_hash(
        const nlohmann::json& action,
        const std::optional<std::string>& vault_address,
        uint64_t nonce
    );
    
    nlohmann::json construct_phantom_agent(
        const std::vector<uint8_t>& hash,
        bool is_mainnet
    );
    
    nlohmann::json sign_inner(const nlohmann::json& typed_data);
    
    std::vector<uint8_t> address_to_bytes(const std::string& address);
    
    // Specific request signers
    nlohmann::json sign_order_params(
        const nlohmann::json& params,
        const std::string& base_url,
        uint64_t timestamp
    );
    
    nlohmann::json sign_cancel_params(
        const nlohmann::json& params,
        const std::string& base_url,
        uint64_t timestamp
    );
    
    nlohmann::json sign_update_leverage_params(
        const nlohmann::json& params,
        const std::string& base_url,
        uint64_t timestamp
    );
};

} // namespace latentspeed::connector
```

**Implementation snippet** (`src/connector/hyperliquid/hyperliquid_auth.cpp`):

```cpp
nlohmann::json HyperliquidAuth::sign_order_params(
    const nlohmann::json& params,
    const std::string& base_url,
    uint64_t timestamp
) {
    // Extract order from params
    const auto& order = params["orders"];
    const auto& grouping = params["grouping"];
    
    // Build order action (Hummingbot pattern)
    nlohmann::json order_action = {
        {"type", "order"},
        {"orders", {order_spec_to_order_wire(order)}},
        {"grouping", grouping}
    };
    
    // Sign the action
    auto signature = sign_l1_action(
        order_action,
        use_vault_ ? std::optional<std::string>(api_key_) : std::nullopt,
        timestamp,
        base_url.find("https://api.hyperliquid.xyz") != std::string::npos
    );
    
    // Build signed payload
    return {
        {"action", order_action},
        {"nonce", timestamp},
        {"signature", signature},
        {"vaultAddress", use_vault_ ? api_key_ : nullptr}
    };
}

std::vector<uint8_t> HyperliquidAuth::action_hash(
    const nlohmann::json& action,
    const std::optional<std::string>& vault_address,
    uint64_t nonce
) {
    // 1. msgpack serialize action
    std::vector<uint8_t> data = msgpack::pack(action);
    
    // 2. Append nonce (8 bytes, big-endian)
    for (int i = 7; i >= 0; --i) {
        data.push_back((nonce >> (i * 8)) & 0xFF);
    }
    
    // 3. Append vault flag and address
    if (!vault_address.has_value()) {
        data.push_back(0x00);
    } else {
        data.push_back(0x01);
        auto vault_bytes = address_to_bytes(*vault_address);
        data.insert(data.end(), vault_bytes.begin(), vault_bytes.end());
    }
    
    // 4. Keccak256 hash
    return keccak256(data);
}
```

---

## 2. Hyperliquid Web Utils

**File**: `include/connector/hyperliquid/hyperliquid_web_utils.h`

```cpp
#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace latentspeed::connector::hyperliquid {

/**
 * @brief Convert float to wire format (8 decimal precision, normalized)
 * 
 * From Hummingbot's float_to_wire function:
 * - Format to 8 decimals
 * - Validate rounding error
 * - Normalize (remove trailing zeros)
 */
std::string float_to_wire(double value);

/**
 * @brief Convert order spec to wire format
 * 
 * Transforms internal order representation to Hyperliquid's wire protocol:
 * {
 *   "a": asset_index,
 *   "b": isBuy,
 *   "p": price_string,
 *   "s": size_string,
 *   "r": reduceOnly,
 *   "t": orderType,
 *   "c": cloid
 * }
 */
nlohmann::json order_spec_to_order_wire(const nlohmann::json& order_spec);

/**
 * @brief Convert order type to wire format
 */
nlohmann::json order_type_to_wire(const nlohmann::json& order_type);

/**
 * @brief Convert order type to tuple (for hashing)
 */
std::pair<int, double> order_type_to_tuple(const nlohmann::json& order_type);

/**
 * @brief Convert float to int for hashing (8 decimals)
 */
int64_t float_to_int_for_hashing(double value);

/**
 * @brief Convert grouping string to number
 */
int order_grouping_to_number(const std::string& grouping);

} // namespace latentspeed::connector::hyperliquid
```

---

## 3. dYdX v4 Auth Module

Based on actual Hummingbot implementation at:
`sub/hummingbot/hummingbot/connector/derivative/dydx_v4_perpetual/data_sources/dydx_v4_data_source.py`

**File**: `include/connector/dydx_v4/dydx_v4_client.h`

```cpp
#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <grpcpp/grpcpp.h>
#include "connector/dydx_v4/dydx_v4_types.h"
#include "connector/dydx_v4/private_key.h"

namespace latentspeed::connector {

/**
 * @class DydxV4Client
 * @brief dYdX v4 client using gRPC and Cosmos SDK
 * 
 * Implements the exact flow from Hummingbot:
 * 1. Mnemonic → private key derivation
 * 2. gRPC channel setup
 * 3. Account query for sequence/number
 * 4. Quantums/subticks conversion
 * 5. Protobuf message construction
 * 6. Cosmos SDK transaction signing
 * 7. Transaction lock for sequence safety
 */
class DydxV4Client {
public:
    DydxV4Client(
        const std::string& mnemonic,
        const std::string& chain_address,
        int subaccount_num = 0
    );

    // ========================================================================
    // ORDER OPERATIONS
    // ========================================================================
    
    /**
     * @brief Place order (async with retry logic)
     * 
     * Implements Hummingbot's place_order with:
     * - Automatic quantums/subticks conversion
     * - goodTilBlock calculation for SHORT_TERM
     * - goodTilBlockTime for LONG_TERM
     * - Retry on sequence mismatch (up to 3 times)
     */
    Task<nlohmann::json> place_order(
        const std::string& market,
        const std::string& type,        // "MARKET" or "LIMIT"
        const std::string& side,        // "BUY" or "SELL"
        double price,
        double size,
        int client_id,
        bool post_only,
        bool reduce_only = false,
        int good_til_time_in_seconds = 6000
    );
    
    /**
     * @brief Cancel order
     */
    Task<nlohmann::json> cancel_order(
        int client_id,
        int clob_pair_id,
        int order_flags,
        uint64_t good_til_block_time
    );

    // ========================================================================
    // CONVERSION UTILITIES (HUMMINGBOT PATTERN)
    // ========================================================================
    
    /**
     * @brief Calculate quantums from human-readable size
     * 
     * Formula: quantums = size * 10^(-atomic_resolution)
     * Must be >= step_base_quantums
     */
    static int64_t calculate_quantums(
        double size,
        int atomic_resolution,
        int step_base_quantums
    );
    
    /**
     * @brief Calculate subticks from human-readable price
     * 
     * Formula: subticks = price * 10^(atomic_resolution - quantum_conversion_exponent - 6)
     * Must be >= subticks_per_tick
     */
    static int64_t calculate_subticks(
        double price,
        int atomic_resolution,
        int quantum_conversion_exponent,
        int subticks_per_tick
    );
    
    /**
     * @brief Calculate good-til-block-time from seconds
     */
    uint64_t calculate_good_til_block_time(int good_til_time_in_seconds);

    // ========================================================================
    // ACCOUNT MANAGEMENT
    // ========================================================================
    
    /**
     * @brief Initialize trading account (query sequence and number)
     */
    Task<void> initialize_trading_account();
    
    /**
     * @brief Get latest block height
     */
    Task<uint64_t> latest_block_height();

private:
    // Authentication
    std::unique_ptr<PrivateKey> private_key_;
    std::string chain_address_;
    int subaccount_num_;
    
    // Account state
    uint64_t sequence_ = 0;
    uint64_t number_ = 0;
    bool is_initialized_ = false;
    
    // Transaction lock (CRITICAL for dYdX)
    std::mutex transaction_lock_;
    
    // gRPC clients
    std::unique_ptr<grpc::Channel> grpc_channel_;
    std::unique_ptr<AuthQueryStub> auth_client_;
    std::unique_ptr<TxServiceStub> tx_client_;
    std::unique_ptr<TendermintServiceStub> tendermint_client_;
    
    // Internal helpers
    uint64_t get_and_increment_sequence();
    Task<void> query_account();
    
    /**
     * @brief Prepare and broadcast transaction (with lock)
     * 
     * This is the critical method that:
     * 1. Acquires transaction_lock_
     * 2. Gets sequence number
     * 3. Signs transaction
     * 4. Broadcasts via gRPC
     * 5. Handles sequence mismatch
     */
    Task<nlohmann::json> prepare_and_broadcast_transaction(
        Transaction& tx,
        const std::optional<std::string>& memo
    );
    
    Task<nlohmann::json> send_tx_sync_mode(const BroadcastTxRequest& request);
    
    std::pair<uint64_t, uint64_t> generate_good_til_fields(
        int order_flags,
        uint64_t good_til_block,
        int good_til_time_in_seconds
    );
};

} // namespace latentspeed::connector
```

**Implementation snippet**:

```cpp
Task<nlohmann::json> DydxV4Client::place_order(
    const std::string& market,
    const std::string& type,
    const std::string& side,
    double price,
    double size,
    int client_id,
    bool post_only,
    bool reduce_only,
    int good_til_time_in_seconds
) {
    // Get market metadata
    const auto& market_info = connector_->get_market_info(market);
    
    // Convert to protocol units
    int64_t quantums = calculate_quantums(
        size,
        market_info.atomic_resolution,
        market_info.step_base_quantums
    );
    
    int64_t subticks = calculate_subticks(
        price,
        market_info.atomic_resolution,
        market_info.quantum_conversion_exponent,
        market_info.subticks_per_tick
    );
    
    // Determine order flags
    int order_flags = (type == "MARKET") 
        ? ORDER_FLAGS_SHORT_TERM 
        : ORDER_FLAGS_LONG_TERM;
    
    // Calculate good-til fields
    uint64_t good_til_block = 0;
    uint64_t good_til_block_time = 0;
    
    if (type == "MARKET") {
        auto latest_block = co_await latest_block_height();
        good_til_block = latest_block + 10;  // Hummingbot uses +10
        good_til_block_time = 0;
    } else {
        good_til_block = 0;
        good_til_block_time = calculate_good_til_block_time(good_til_time_in_seconds);
    }
    
    // Build protobuf Order
    Order order;
    // ... (populate protobuf fields)
    
    // Build MsgPlaceOrder
    MsgPlaceOrder msg;
    msg.set_allocated_order(&order);
    
    // Retry loop (Hummingbot pattern)
    for (int i = 0; i < 3; ++i) {
        auto result = co_await send_message(msg);
        
        if (result["raw_log"].get<std::string>().find("sequence mismatch") != std::string::npos) {
            LOG_WARN("Sequence mismatch, retrying ({}/3)", i + 1);
            co_await initialize_trading_account();  // Re-fetch sequence
            await std::chrono::seconds(1);
            continue;
        }
        
        co_return result;
    }
    
    throw std::runtime_error("Failed to place order after 3 retries");
}

Task<nlohmann::json> DydxV4Client::prepare_and_broadcast_transaction(
    Transaction& tx,
    const std::optional<std::string>& memo
) {
    // LOCK ACQUISITION (critical for dYdX)
    std::lock_guard<std::mutex> lock(transaction_lock_);
    
    // Get sequence
    uint64_t seq = get_and_increment_sequence();
    uint64_t num = number_;
    
    // Seal transaction
    tx.seal(
        SigningCfg::direct(*private_key_, seq),
        fee = "25000000000000000adv4tnt",
        gas_limit = 1000000,
        memo = memo
    );
    
    // Sign
    tx.sign(*private_key_, CHAIN_ID, num);
    tx.complete();
    
    // Broadcast
    BroadcastTxRequest request;
    request.set_tx_bytes(tx.serialize());
    request.set_mode(BroadcastMode::BROADCAST_MODE_SYNC);
    
    auto result = co_await send_tx_sync_mode(request);
    
    // Handle sequence mismatch
    if (result["raw_log"].get<std::string>().find("sequence mismatch") != std::string::npos) {
        co_await initialize_trading_account();
    }
    
    co_return result;
}
```

---

## 4. Libraries Required

### For Hyperliquid

```cmake
# EIP-712 and Ethereum crypto
find_package(ethash CONFIG REQUIRED)
find_package(secp256k1 CONFIG REQUIRED)

# msgpack for action hashing
find_package(msgpack CONFIG REQUIRED)
```

### For dYdX v4

```cmake
# gRPC and Protobuf
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

# v4-proto (dYdX protobuf definitions)
# This needs to be compiled from: https://github.com/dydxprotocol/v4-proto
```

---

## Next: Phase 5 - Event Lifecycle

See [06_PHASE5_EVENT_LIFECYCLE.md](06_PHASE5_EVENT_LIFECYCLE.md) for:
- Complete order placement flow
- Event emission patterns
- Error handling strategies
