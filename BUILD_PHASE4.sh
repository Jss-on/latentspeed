#!/bin/bash
# Quick build script for Phase 4 tests

set -e

echo "Building Phase 4: Hyperliquid Auth & Web Utils"
echo "==============================================="

cd "$(dirname "$0")"

# Build the test
echo "Building test_hyperliquid_utils..."
cmake --build build/release --target test_hyperliquid_utils -j$(nproc)

echo ""
echo "Build successful! Running tests..."
echo ""

# Run the test
./build/release/tests/unit/connector/test_hyperliquid_utils

echo ""
echo "==============================================="
echo "Phase 4 tests complete!"
echo ""
echo "NOTE: Crypto functions are PLACEHOLDERS"
echo "For production trading, implement:"
echo "  - libsecp256k1 (ECDSA signing)"
echo "  - keccak256 (Ethereum hashing)"
echo "  - msgpack (serialization)"
echo ""
echo "OR use external signer (Python/TypeScript)"
echo "==============================================="
