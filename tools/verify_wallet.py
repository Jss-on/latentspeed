#!/usr/bin/env python3
"""Verify which wallet address corresponds to a private key"""

import sys
from eth_account import Account

# Your target wallet address (derived from your private key)
target_address = "0x85c9cF5AEdd0B1C642Ac0023F780Fbb06B94A754"

# Prompt for private key (don't hardcode it)
if len(sys.argv) > 1:
    private_key = sys.argv[1]
else:
    print("Usage: python3 verify_wallet.py <private_key>")
    print(f"\nTarget wallet: {target_address}")
    print("\nProvide the private key to verify it matches this address")
    sys.exit(1)

if not private_key.startswith("0x"):
    private_key = "0x" + private_key

try:
    # Derive the actual wallet address
    wallet = Account.from_key(private_key)
    derived_address = wallet.address

    print("=" * 60)
    print("WALLET ADDRESS VERIFICATION")
    print("=" * 60)
    print(f"Private Key:      {private_key[:10]}...{private_key[-8:]}")  # Partial display for security
    print(f"Derived Address:  {derived_address}")
    print(f"Target Address:   {target_address}")
    print("=" * 60)

    if derived_address.lower() == target_address.lower():
        print("✅ MATCH: Private key matches target wallet address")
        print("\nYou can now use this in your config:")
        print(f"  wallet_address: \"{derived_address}\"")
        print(f"  private_key: \"{private_key}\"")
    else:
        print("❌ MISMATCH: Private key does NOT match target wallet address")
        print(f"\nThe private key actually corresponds to: {derived_address}")
        print(f"\nYou need the correct private key for: {target_address}")
    print("=" * 60)
    
except Exception as e:
    print(f"Error: {e}")
    sys.exit(1)
