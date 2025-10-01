#!/bin/bash
# ==========================================================
# Latentspeed Trading Engine build helper (Linux/WSL)
#   run.sh [--debug|--release] [--clean] [--docs] [--market-data] [--test]
# ==========================================================

# ---- defaults ----
BUILD_TYPE="Debug"
PRESET_NAME="linux-debug"
CLEAN=""
BUILD_DOCS=""
BUILD_MARKET_DATA=""
RUN_TESTS=""

# ---- parse CLI flags ----
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            PRESET_NAME="linux-debug"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            PRESET_NAME="linux-release"
            shift
            ;;
        --clean)
            CLEAN="1"
            shift
            ;;
        --docs)
            BUILD_DOCS="1"
            shift
            ;;
        --market-data)
            BUILD_MARKET_DATA="1"
            shift
            ;;
        --test)
            RUN_TESTS="1"
            shift
            ;;
        *)
            echo "Unknown option $1"
            echo "Usage: $0 [--debug|--release] [--clean] [--docs] [--market-data] [--test]"
            echo "  --debug       Build in Debug mode (default)"
            echo "  --release     Build in Release mode"
            echo "  --clean       Clean build directory first"
            echo "  --docs        Build documentation"
            echo "  --market-data Build and test market data provider"
            echo "  --test        Run market data tests after build"
            exit 1
            ;;
    esac
done

# ---- paths ----
SCRIPT_DIR="$(dirname "$0")"
BUILD_DIR="$SCRIPT_DIR/build"
VCPKG_DIR="$SCRIPT_DIR/external/vcpkg"

echo
echo "****  Building Latentspeed Trading Engine ($BUILD_TYPE)  ****"
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
VCPKG_TRIPLET="x64-linux"

# Install dependencies from vcpkg.json
"$VCPKG_DIR/vcpkg" install --triplet=$VCPKG_TRIPLET || exit 1

# ---- optional clean ----
if [ -n "$CLEAN" ]; then
    echo "Cleaning build directory for preset $PRESET_NAME..."
    rm -rf "$SCRIPT_DIR/build"
fi

# ---- configure with CMake preset ----
echo "Configuring with CMake preset: $PRESET_NAME..."
cd "$SCRIPT_DIR"

cmake --preset="$PRESET_NAME" || exit 1

# ---- build with CMake preset ----
echo "Building with CMake preset: $PRESET_NAME..."
cmake --build --preset="$PRESET_NAME" || exit 1

# ---- build docs if requested ----
if [ -n "$BUILD_DOCS" ]; then
    echo "Building documentation..."
    # Add documentation build command here if needed
    echo "Documentation build not yet implemented"
fi

echo
echo "Build completed successfully!"
echo "Preset used: $PRESET_NAME"
echo "Executables built:"
echo "  Trading Engine: $SCRIPT_DIR/build/$PRESET_NAME/trading_engine_service"
echo "  Market Data Test: $SCRIPT_DIR/build/$PRESET_NAME/test_market_data"
echo

# ---- market data provider testing ----
if [ -n "$BUILD_MARKET_DATA" ] || [ -n "$RUN_TESTS" ]; then
    echo "=== Market Data Provider Setup ==="
    
    # Check if test executable exists
    if [ -x "$SCRIPT_DIR/build/$PRESET_NAME/test_market_data" ]; then
        echo "Market data test executable ready!"
        echo
        echo "ZMQ Ports:"
        echo "  5556 - Trades stream"
        echo "  5557 - OrderBook stream (10 levels)"
        echo
        echo "Usage examples:"
        echo "  # Test with Bybit (default symbols BTCUSDT,ETHUSDT)"
        echo "  cd $SCRIPT_DIR/build/$PRESET_NAME"
        echo "  ./test_market_data bybit"
        echo
        echo "  # Test with custom symbols"
        echo "  ./test_market_data bybit BTCUSDT,ETHUSDT,SOLUSDT"
        echo
        echo "  # Subscribe to ZMQ streams (requires Python pyzmq)"
        echo "  python3 ../../test_zmq_subscriber.py --duration 30"
        echo
        
        if [ -n "$RUN_TESTS" ]; then
            echo "=== Running Market Data Tests ==="
            
            # Check if Python ZMQ is available
            if python3 -c "import zmq" 2>/dev/null; then
                echo "Running comprehensive market data test (30 seconds)..."
                chmod +x "$SCRIPT_DIR/run_market_data_test.sh"
                "$SCRIPT_DIR/run_market_data_test.sh" bybit BTCUSDT,ETHUSDT 30
            else
                echo "Python ZMQ not available. Running basic test..."
                echo "Starting market data provider for 10 seconds..."
                cd "$SCRIPT_DIR/build/$PRESET_NAME"
                timeout 10s ./test_market_data bybit BTCUSDT,ETHUSDT || echo "Test completed"
                cd "$SCRIPT_DIR"
            fi
        fi
    else
        echo "Market data test executable not found at $SCRIPT_DIR/build/$PRESET_NAME/test_market_data"
        echo "Make sure the build completed successfully."
    fi
    echo
fi

echo "To run the trading engine service:"
echo "  cd $SCRIPT_DIR/build/$PRESET_NAME"
echo "  ./trading_engine_service --exchange bybit --api-key YOUR_KEY --api-secret YOUR_SECRET"
echo
echo "To enable market data in trading engine, add: --enable-market-data"
echo
