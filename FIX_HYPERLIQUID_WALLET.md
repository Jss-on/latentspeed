# Fix: Hyperliquid Wallet Not Registered on Testnet

## Problem
Orders are failing with error: `"User or API Wallet does not exist"`

## Root Cause
Your wallet **0x85c9cF5AEdd0B1C642Ac0023F780Fbb06B94A754** is not registered on Hyperliquid testnet.

## ✅ Credentials Now Correct
- **Wallet Address**: `0x85c9cF5AEdd0B1C642Ac0023F780Fbb06B94A754`
- **Private Key**: `0x2e5aacf85446088b3121eec4eab06beda234decc4d16ffe3cb0d2a5ec25ea60b`
- **Match Status**: ✅ Verified matching pair

## Solution: Register Your Wallet on Hyperliquid Testnet

### Step 1: Stop Current Trading Engine
In the terminal running the trading engine, press **Ctrl+C** to stop it.

### Step 2: Register Wallet on Hyperliquid Testnet
Visit **https://app.hyperliquid-testnet.xyz/** and:

1. **Connect your wallet** or use the private key to access:
   - **Wallet Address**: `0x85c9cF5AEdd0B1C642Ac0023F780Fbb06B94A754`
   - **Private Key**: `0x2e5aacf85446088b3121eec4eab06beda234decc4d16ffe3cb0d2a5ec25ea60b`

2. **Complete onboarding** - Follow Hyperliquid's testnet registration process

3. **Get testnet funds** (if needed) - Look for a faucet or airdrop option

4. **Verify wallet is active** - Check that your wallet shows up in their system

### Step 3: Restart Trading Engine with Correct Address
```bash
cd /home/tensor/latentspeed

sudo ./build/release/trading_engine_service \
  --exchange hyperliquid \
  --api-key 0x85c9cF5AEdd0B1C642Ac0023F780Fbb06B94A754 \
  --api-secret 0x2e5aacf85446088b3121eec4eab06beda234decc4d16ffe3cb0d2a5ec25ea60b \
  --testnet
```

### Step 4: Test Again
```bash
python3 examples/test_engine.py
```

## Verification
Run this to confirm your wallet address is correctly derived:
```bash
python3 tools/verify_wallet.py
```

Expected output:
```
✅ MATCH: Private key matches config wallet address
```

## Updated Configuration Files
The following files have been updated with the correct wallet address:
- ✅ `configs/hyperliquid_credentials.yml`
- ✅ `START_TRADING_SYSTEM.md`

## Alternative: Use a Different Wallet
If you have an existing registered Hyperliquid testnet wallet:
1. Get the private key for that wallet
2. Update `configs/hyperliquid_credentials.yml` with both the address AND private key
3. Restart the trading engine with those credentials

## Important Notes
- **Testnet vs Mainnet**: Make sure you're using the testnet (`https://api.hyperliquid-testnet.xyz`)
- **Wallet must exist**: Hyperliquid requires the wallet to be registered/active before accepting orders
- **Keep private key secure**: Never share or commit your private key to version control
