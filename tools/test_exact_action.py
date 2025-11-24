#!/usr/bin/env python3
"""Test signing with the EXACT action from C++ logs"""

from eth_account import Account
from hyperliquid.utils.signing import sign_l1_action
import json

private_key = "0x2e5aacf85446088b3121eec4eab06beda234decc4d16ffe3cb0d2a5ec25ea60b"
expected_wallet = "0x85c9cF5AEdd0B1C642Ac0023F780Fbb06B94A754"

# Exact action from C++ logs (alphabetically ordered by nlohmann::json)
action_alphabetical = {
    "grouping": "na",
    "orders": [{
        "a": 4,
        "b": True,
        "c": "0x10b3bd91f11446aa795be1757a44d0c4",
        "p": "1000000.0",
        "r": False,
        "s": "0.001",
        "t": {"limit": {"tif": "Ioc"}}
    }],
    "type": "order"
}

# Canonical order (as Python SDK might expect)
action_canonical = {
    "type": "order",
    "grouping": "na",
    "orders": [{
        "a": 4,
        "b": True,
        "p": "1000000.0",
        "s": "0.001",
        "r": False,
        "t": {"limit": {"tif": "Ioc"}},
        "c": "0x10b3bd91f11446aa795be1757a44d0c4"
    }]
}

nonce = 1763560564734
wallet = Account.from_key(private_key)

print(f"Wallet: {wallet.address}")
print(f"Expected: {expected_wallet}")
print(f"Match: {wallet.address.lower() == expected_wallet.lower()}\n")

# Test 1: Alphabetical order (as C++ sends it)
print("=" * 60)
print("Test 1: Alphabetical order (as C++ nlohmann::json dumps it)")
print("=" * 60)
sig1 = sign_l1_action(wallet, action_alphabetical, None, nonce, None, False)
print(f"Signature r: {sig1['r']}")
print(f"Signature s: {sig1['s']}")
print(f"Signature v: {sig1['v']}")

# Test 2: Canonical order
print("\n" + "=" * 60)
print("Test 2: Canonical order (type, grouping, orders)")
print("=" * 60)
sig2 = sign_l1_action(wallet, action_canonical, None, nonce, None, False)
print(f"Signature r: {sig2['r']}")
print(f"Signature s: {sig2['s']}")
print(f"Signature v: {sig2['v']}")

# Compare
print("\n" + "=" * 60)
print("Comparison")
print("=" * 60)
if sig1['r'] == sig2['r'] and sig1['s'] == sig2['s']:
    print("✅ Same signature - key order doesn't matter")
else:
    print("❌ DIFFERENT signatures - key order DOES matter!")
    print("\nThis is the problem! nlohmann::json reorders keys alphabetically,")
    print("but Hyperliquid expects a specific canonical order.")
