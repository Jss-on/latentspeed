# Hyperliquid Order Placement Fixes Applied

## Summary

Applied critical fixes from the working `hyperliquid_adapter` branch to fix the "User or API Wallet does not exist" error.

## Root Cause

The error was caused by a **wallet address mismatch**:
- Config had: `0x44Fd91bEd5c87A4fFA222462798BB9d7Ef3669be`  
- Private key derives to: `0xb9be8694f0d21f5eabb44f38114127ba582b1e30`

When the Python signer derives the wallet address from the private key, it doesn't match the configured address, causing Hyperliquid to reject the signature.

## Fixes Applied

### 1. ✅ Fixed Wallet Address in Config

**File**: `configs/hyperliquid_credentials.yml`

Updated wallet_address to match the address derived from the private key:
```yaml
wallet_address: "0xb9be8694f0d21f5eabb44f38114127ba582b1e30"  # Derived from private key
private_key: "0x2e5aacf85446088b3121eec4eab06beda234decc4d16ffe3cb0d2a5ec25ea60b"
```

### 2. ✅ Added `build_action_payload` Method

**Why**: Ensures ZERO divergence between what's signed and what's posted to the API.

**Files Modified**:
- `include/adapters/hyperliquid_signer.h` - Added method to interface
- `include/adapters/python_hl_signer.h` - Added method declaration  
- `src/adapters/python_hl_signer.cpp` - Implemented the method
- `tools/hl_signer_bridge.py` - Added Python handler

The method builds the complete JSON payload (`{action, nonce, signature, vaultAddress}`) via the Python SDK, ensuring it matches exactly what was signed.

### 3. ✅ Updated `HyperliquidAuth` to Use `build_action_payload`

**File**: `src/connector/exchange/hyperliquid/hyperliquid_auth.cpp`

Changed from manually constructing the signed payload to using the signer's `build_action_payload`:

**Before**:
```cpp
auto sig = signer_->sign_l1_action(...);
nlohmann::json signed_action = {
    {"action", action},
    {"nonce", nonce},
    {"signature", {...}}  // ← Manual construction can diverge
};
```

**After**:
```cpp
auto payload_str = signer_->build_action_payload(...);
nlohmann::json signed_action = nlohmann::json::parse(*payload_str);
// ← SDK builds exact payload that was signed
```

### 4. ✅ Market Order Price Fix (Already Done)

Using extreme prices for market orders (IOC):
- BUY: `$1,000,000` (matches any ask)
- SELL: `$0.01` (matches any bid)

### 5. ✅ Client Order ID Format Fix (Already Done)

Converting to 128-bit hex: `0x1234...` (32 hex chars)

## What You Need to Do Next

### Step 1: Fund the Wallet on Testnet

The wallet `0xb9be8694f0d21f5eabb44f38114127ba582b1e30` needs to be funded on Hyperliquid testnet.

**Option A**: Transfer funds from another testnet wallet  
**Option B**: Get a new wallet with both address and private key from https://app.hyperliquid-testnet.xyz

### Step 2: Verify the Wallet Match

```bash
cd /home/tensor/latentspeed
python3 tools/verify_wallet.py
```

Expected output:
```
✅ MATCH: Private key matches config wallet address
```

### Step 3: Rebuild the Project

```bash
cd /home/tensor/latentspeed
./run.sh --release
```

### Step 4: Run with Testnet Mode

```bash
# Set environment variables (with LATENTSPEED_ prefix)
export LATENTSPEED_HYPERLIQUID_USER_ADDRESS=0xb9be8694f0d21f5eabb44f38114127ba582b1e30
export LATENTSPEED_HYPERLIQUID_PRIVATE_KEY=0x2e5aacf85446088b3121eec4eab06beda234decc4d16ffe3cb0d2a5ec25ea60b
export LATENTSPEED_HYPERLIQUID_USE_TESTNET=true

# Run trading engine (omit --live-trade flag for testnet)
./build/release/trading_engine_service \
  --exchange hyperliquid \
  --api-key $LATENTSPEED_HYPERLIQUID_USER_ADDRESS \
  --api-secret $LATENTSPEED_HYPERLIQUID_PRIVATE_KEY
```

Or simpler (reads from env automatically):
```bash
./build/release/trading_engine_service --exchange hyperliquid
```

## Expected Result

Once the wallet is funded on testnet, orders should succeed:

```
[HL-REST] Response status: 200 OK
[HL-REST] Response body: {"status":"ok","response":{"data":{"statuses":[{"resting":{"oid":123456}}]}}}
✅ Order placed successfully!
```

## Key Differences from Working Branch

The working branch (`hyperliquid_adapter`) uses this pattern throughout:
1. **No address derivation** - trusts user provides matching wallet/key pair
2. **build_action_payload** - ensures SDK constructs exact payload 
3. **Vault address optional** - only for subaccount trading
4. **Lowercase normalization** - via `util::to_lower_hex_address()`

## Troubleshooting

### Still Getting "User does not exist"

1. **Verify wallet match**:
   ```bash
   python3 tools/verify_wallet.py
   ```

2. **Check wallet has funds on testnet**:
   - Visit: https://app.hyperliquid-testnet.xyz
   - Connect wallet `0xb9be8694f0d21f5eabb44f38114127ba582b1e30`
   - Check balance

3. **Verify testnet mode is active**:
   - Look for log: `network=testnet` in startup logs
   - API URL should be: `https://api.hyperliquid-testnet.xyz`

### Other Issues

Check logs at:
- `logs/trading_engine.log` - Order placement logs
- Python signer output - Should see `[PythonHyperliquidSigner] Payload generated successfully`

---

**All fixes have been applied!** Just need to rebuild and fund the wallet on testnet.
