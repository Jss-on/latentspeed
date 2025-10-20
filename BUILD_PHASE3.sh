#!/bin/bash
# Quick build script for Phase 3 tests

set -e

echo "Building Phase 3: OrderBook & Data Sources"
echo "==========================================="

cd "$(dirname "$0")"

# Build the test
echo "Building test_order_book..."
cmake --build build/release --target test_order_book -j$(nproc)

echo ""
echo "Build successful! Running tests..."
echo ""

# Run the test
./build/release/tests/unit/connector/test_order_book

echo ""
echo "==========================================="
echo "Phase 3 tests complete!"
echo "==========================================="
