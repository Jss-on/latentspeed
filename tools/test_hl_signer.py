#!/usr/bin/env python3
"""Test Hyperliquid signer to verify signature recovery"""

from eth_account import Account
from hyperliquid.utils.signing import sign_l1_action

# Your credentials
private_key = "0x2e5aacf85446088b3121eec4eab06beda234decc4d16ffe3cb0d2a5ec25ea60b"
expected_address = "0x85c9cF5AEdd0B1C642Ac0023F780Fbb06B94A754"

# Create wallet
wallet = Account.from_key(private_key)
print(f"Wallet address: {wallet.address}")
print(f"Expected: {expected_address}")
print(f"Match: {wallet.address.lower() == expected_address.lower()}")
print()

# Test action (simple order)
action = {
    "type": "order",
    "grouping": "na",
    "orders": [{
        "a": 4,  # ETH
        "b": True,  # buy
        "p": "1000000.0",
        "s": "0.001",
        "r": False,
        "t": {"limit": {"tif": "Ioc"}},
        "c": "0xe195d89491c49df126732b51cdeb87af"
    }]
}

nonce = 1763559861829
is_mainnet = False  # testnet

print(f"Signing action with nonce {nonce}, mainnet={is_mainnet}")
sig = sign_l1_action(wallet, action, None, nonce, None, is_mainnet)

print(f"\nSignature:")
print(f"  r: {sig['r']}")
print(f"  s: {sig['s']}")
print(f"  v: {sig['v']}")

# Verify padding
r_len = len(sig['r'])
s_len = len(sig['s'])
print(f"\nLength check:")
print(f"  r: {r_len} chars (should be 66: 0x + 64)")
print(f"  s: {s_len} chars (should be 66: 0x + 64)")

if r_len != 66 or s_len != 66:
    print("\n⚠️  WARNING: Signature values not properly padded!")
else:
    print("\n✅ Signature values properly padded")

# Build complete payload
payload = {
    "action": action,
    "nonce": nonce,
    "signature": sig
}

import json
print(f"\nFull payload:")
print(json.dumps(payload, separators=(',', ':')))
