#!/bin/bash
# Quick build script for Phase 2 tests

set -e

echo "Building Phase 2: Order Tracking & State Management"
echo "===================================================="

cd "$(dirname "$0")"

# Build the test
echo "Building test_order_tracking..."
cmake --build build/release --target test_order_tracking -j$(nproc)

echo ""
echo "Build successful! Running tests..."
echo ""

# Run the test
./build/release/tests/unit/connector/test_order_tracking

echo ""
echo "===================================================="
echo "Phase 2 tests complete!"
echo "===================================================="
