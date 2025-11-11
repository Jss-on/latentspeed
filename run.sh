#!/bin/bash
# ==========================================================
# LatentSpeed Trading System build helper (Linux/WSL)
#   run.sh [--debug|--release] [--clean]
# 
# Builds two production executables:
#   - marketstream          (market data provider)
#   - trading_engine_service (trading engine)
# ==========================================================

# ---- defaults ----
BUILD_TYPE="Debug"
PRESET_NAME="linux-debug"
CLEAN=""
TARGET_TRIPLET="x64-linux"
HOST_TRIPLET="x64-linux"

# ---- parse CLI flags ----
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            PRESET_NAME="linux-debug"
            TARGET_TRIPLET="x64-linux"
            HOST_TRIPLET="x64-linux"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            PRESET_NAME="linux-release"
            TARGET_TRIPLET="x64-linux"
            HOST_TRIPLET="x64-linux"
            shift
            ;;
        --clean)
            CLEAN="1"
            shift
            ;;
        *)
            echo "Unknown option $1"
            echo "Usage: $0 [--debug|--release] [--clean]"
            echo "  --debug       Build in Debug mode (default)"
            echo "  --release     Build in Release mode (production)"
            echo "  --clean       Clean build directory first"
            exit 1
            ;;
    esac
done

# ---- paths ----
SCRIPT_DIR="$(dirname "$0")"
BUILD_DIR="$SCRIPT_DIR/build"
VCPKG_DIR="$SCRIPT_DIR/external/vcpkg"

echo
echo "****  Building LatentSpeed Trading System ($BUILD_TYPE)  ****"
echo

# ---- Check if vcpkg exists ----
if [ ! -d "$VCPKG_DIR" ]; then
    echo "Setting up vcpkg..."
    git clone https://github.com/Microsoft/vcpkg.git "$VCPKG_DIR" || exit 1
fi

# ---- bootstrap vcpkg (first run) ----
if [ ! -f "$VCPKG_DIR/vcpkg" ]; then
    echo "Bootstrapping vcpkg..."
    "$VCPKG_DIR/bootstrap-vcpkg.sh" || exit 1
fi

# ---- Check for required system dependencies ----
echo "Checking system dependencies..."

# Check for required packages
MISSING_DEPS=""

# Check for cmake
if ! command -v cmake &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS cmake"
fi

# Check for ninja
if ! command -v ninja &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS ninja-build"
fi

# Check for ccache (optional but recommended)
if ! command -v ccache &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS ccache"
fi

# Check for pkg-config
if ! command -v pkg-config &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS pkg-config"
fi

# Check for essential build tools
if ! command -v gcc &> /dev/null && ! command -v clang &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS build-essential"
fi

# Check for git (needed for vcpkg and submodules)
if ! command -v git &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS git"
fi

# Check for curl development headers (needed for some vcpkg packages)
if ! pkg-config --exists libcurl; then
    MISSING_DEPS="$MISSING_DEPS libcurl4-openssl-dev"
fi

# Check for OpenSSL development headers
if ! pkg-config --exists openssl; then
    MISSING_DEPS="$MISSING_DEPS libssl-dev"
fi

# Check for ZMQ development headers
if ! pkg-config --exists libzmq; then
    MISSING_DEPS="$MISSING_DEPS libzmq3-dev"
fi

# Additional dependencies for building some vcpkg packages
if ! pkg-config --exists zlib; then
    MISSING_DEPS="$MISSING_DEPS zlib1g-dev"
fi

if [ -n "$MISSING_DEPS" ]; then
    echo "Missing system dependencies. Please install:"
    echo "  sudo apt-get update"
    echo "  sudo apt-get install$MISSING_DEPS"
    exit 1
fi

echo "All system dependencies found."

# ---- Install vcpkg dependencies ----
echo "Installing vcpkg dependencies..."

echo "Using vcpkg target triplet: $TARGET_TRIPLET"
echo "Using vcpkg host triplet:   $HOST_TRIPLET"

# Install dependencies from vcpkg.json
"$VCPKG_DIR/vcpkg" install \
    --triplet="$TARGET_TRIPLET" \
    --host-triplet="$HOST_TRIPLET" || exit 1

# ---- optional clean ----
if [ -n "$CLEAN" ]; then
    echo "Cleaning build directory for preset $PRESET_NAME..."
    rm -rf "$SCRIPT_DIR/build"
fi

# ---- configure with CMake preset ----
echo "Configuring with CMake preset: $PRESET_NAME..."
cd "$SCRIPT_DIR"

VCPKG_TARGET_TRIPLET="$TARGET_TRIPLET" \
VCPKG_HOST_TRIPLET="$HOST_TRIPLET" \
cmake --preset="$PRESET_NAME" || exit 1

# ---- build with CMake preset ----
echo "Building with CMake preset: $PRESET_NAME..."
VCPKG_TARGET_TRIPLET="$TARGET_TRIPLET" \
VCPKG_HOST_TRIPLET="$HOST_TRIPLET" \
cmake --build --preset="$PRESET_NAME" || exit 1

echo
echo "="
echo "=== Build Completed Successfully! ==="
echo "="
echo "Preset: $PRESET_NAME"
echo
echo "Production Executables:"
echo "  1. MarketStream (Market Data Provider):"
echo "     $SCRIPT_DIR/build/$PRESET_NAME/marketstream"
echo
echo "  2. Trading Engine Service:"
echo "     $SCRIPT_DIR/build/$PRESET_NAME/trading_engine_service"
echo

echo "="
echo "=== Quick Start Guide ==="
echo "="
echo
echo "1. Configure MarketStream:"
echo "   Edit config.yml to add exchanges and symbols"
echo
echo "2. Start MarketStream (Terminal 1):"
echo "   cd $SCRIPT_DIR/build/$PRESET_NAME"
echo "   ./marketstream ../../config.yml"
echo
echo "   Output: ZMQ streams on ports 5556 (trades) and 5557 (orderbooks)"
echo
echo "3. Start Trading Engine (Terminal 2):"
echo "   cd $SCRIPT_DIR/build/$PRESET_NAME"
echo "   ./trading_engine_service \\"
echo "     --exchange bybit \\"
echo "     --api-key YOUR_API_KEY \\"
echo "     --api-secret YOUR_API_SECRET"
echo
echo "   Optional: Add --live-trade for mainnet (default is testnet)"
echo
echo "="
echo "=== ZMQ Endpoints ==="
echo "="
echo
echo "MarketStream Output (market data):"
echo "  tcp://127.0.0.1:5556 - Preprocessed trades"
echo "  tcp://127.0.0.1:5557 - Preprocessed orderbooks"
echo
echo "Trading Engine I/O (order execution):"
echo "  tcp://127.0.0.1:5601 - PULL socket for ExecutionOrders"
echo "  tcp://127.0.0.1:5602 - PUB socket for ExecutionReports/Fills"
echo
echo "="
echo "For production deployment, see: PRODUCTION.md"
echo "="
echo
