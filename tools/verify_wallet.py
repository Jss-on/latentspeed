#!/usr/bin/env python3
"""Verify which wallet address corresponds to a private key"""

from eth_account import Account

# Your private key from config
private_key = "0x2e5aacf85446088b3121eec4eab06beda234decc4d16ffe3cb0d2a5ec25ea60b"

# Derive the actual wallet address
wallet = Account.from_key(private_key)
derived_address = wallet.address

print("=" * 60)
print("WALLET ADDRESS VERIFICATION")
print("=" * 60)
print(f"Private Key:      {private_key}")
print(f"Derived Address:  {derived_address}")
print(f"Config Address:   0x44Fd91bEd5c87A4fFA222462798BB9d7Ef3669be")
print("=" * 60)

if derived_address.lower() == "0x44Fd91bEd5c87A4fFA222462798BB9d7Ef3669be".lower():
    print("✅ MATCH: Private key matches config wallet address")
else:
    print("❌ MISMATCH: Private key does NOT match config wallet address")
    print(f"\nThe private key actually corresponds to: {derived_address}")
    print(f"You need to either:")
    print(f"  1. Use the correct private key for 0x44Fd91bEd5c87A4fFA222462798BB9d7Ef3669be")
    print(f"  2. Update config to use {derived_address} as wallet_address")
    print(f"  3. Get a new wallet with both private key and address")
print("=" * 60)
