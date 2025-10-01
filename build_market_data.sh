#!/bin/bash

# Build script for market data provider
# Usage: ./build_market_data.sh [clean]

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Building Market Data Provider ===${NC}"

# Check if clean build requested
if [[ "$1" == "clean" ]]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf build/
fi

# Create build directory
mkdir -p build
cd build

echo -e "${YELLOW}Installing dependencies with vcpkg...${NC}"

# Install required packages via vcpkg if not already installed
VCPKG_ROOT=${VCPKG_ROOT:-"/opt/vcpkg"}
if [[ -d "$VCPKG_ROOT" ]]; then
    echo "Using vcpkg at: $VCPKG_ROOT"
    
    # Install dependencies
    $VCPKG_ROOT/vcpkg install \
        cppzmq \
        websocketpp \
        rapidjson \
        spdlog \
        curl[core,openssl] \
        openssl \
        boost-system \
        args \
        --triplet=x64-linux
        
    CMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
else
    echo -e "${YELLOW}Warning: vcpkg not found at $VCPKG_ROOT, using system packages${NC}"
    CMAKE_TOOLCHAIN_FILE=""
fi

echo -e "${YELLOW}Configuring CMake...${NC}"

# Configure with CMake
CMAKE_CMD="cmake .."
if [[ -n "$CMAKE_TOOLCHAIN_FILE" ]]; then
    CMAKE_CMD="$CMAKE_CMD -DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE"
fi
CMAKE_CMD="$CMAKE_CMD -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20"

echo "Running: $CMAKE_CMD"
eval $CMAKE_CMD

echo -e "${YELLOW}Building...${NC}"

# Build with multiple cores
make -j$(nproc) test_market_data

if [[ $? -eq 0 ]]; then
    echo -e "${GREEN}Build completed successfully!${NC}"
    echo -e "${GREEN}Executable: $(pwd)/test_market_data${NC}"
    
    # Check if executable exists and is runnable
    if [[ -x "./test_market_data" ]]; then
        echo -e "${GREEN}Test executable is ready to run${NC}"
        
        # Show usage
        echo -e "${BLUE}Usage:${NC}"
        echo "  ./test_market_data [exchange] [symbols]"
        echo "  Examples:"
        echo "    ./test_market_data bybit BTCUSDT,ETHUSDT"
        echo "    ./test_market_data binance BTCUSDT"
        echo ""
        echo -e "${BLUE}ZMQ Ports:${NC}"
        echo "  5556 - Trades stream"
        echo "  5557 - OrderBook stream (10 levels)"
        echo ""
    else
        echo -e "${RED}Error: Executable not found or not executable${NC}"
        exit 1
    fi
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo -e "${BLUE}=== Build Complete ===${NC}"
