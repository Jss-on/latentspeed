#!/bin/bash
# Quick build script for Phase 5: Event-Driven Order Lifecycle

set -e

echo "Building Phase 5: Event-Driven Order Lifecycle"
echo "=============================================="

cd "$(dirname "$0")"

# Build the test
echo "Building test_hyperliquid_connector..."
cmake --build build/release --target test_hyperliquid_connector -j$(nproc)

echo ""
echo "Build successful! Running tests..."
echo ""

# Run the test
./build/release/tests/unit/connector/test_hyperliquid_connector

echo ""
echo "=============================================="
echo "Phase 5 tests complete!"
echo "=============================================="
