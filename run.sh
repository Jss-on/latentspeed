#!/bin/bash
# ==========================================================
# Latentspeed Trading Engine build helper (Linux/WSL)
#   build.sh [--debug|--release] [--clean] [--docs]
# ==========================================================

# ---- defaults ----
BUILD_TYPE="Debug"
CLEAN=""
BUILD_DOCS=""

# ---- parse CLI flags ----
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
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
        *)
            echo "Unknown option $1"
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
    echo "Cleaning $BUILD_DIR ..."
    rm -rf "$BUILD_DIR"
fi

# ---- create build directory ----
mkdir -p "$BUILD_DIR"

# ---- configure with CMake ----
echo "Configuring with CMake..."
cd "$BUILD_DIR"

# Check if ccache is available and configure it
CMAKE_CCACHE_ARGS=""
if command -v ccache &> /dev/null; then
    echo "Using ccache for faster compilation..."
    CMAKE_CCACHE_ARGS="-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache"
fi

cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET" \
    $CMAKE_CCACHE_ARGS \
    -G Ninja || exit 1

# ---- build ----
echo "Building with Ninja..."
ninja || exit 1

# ---- build docs if requested ----
if [ -n "$BUILD_DOCS" ]; then
    echo "Building documentation..."
    # Add documentation build command here if needed
    echo "Documentation build not yet implemented"
fi

echo
echo "Build completed successfully!"
echo "Executable location: $BUILD_DIR/trading_engine_service"
echo
echo "To run the trading engine service:"
echo "  cd $BUILD_DIR"
echo "  ./trading_engine_service"
echo
