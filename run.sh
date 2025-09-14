#!/bin/bash
# ==========================================================
# Latentspeed Trading Engine build helper (Linux/WSL)
#   build.sh [--debug|--release] [--clean] [--docs]
# ==========================================================

# ---- defaults ----
BUILD_TYPE="Debug"
PRESET_NAME="linux-debug"
CLEAN=""
BUILD_DOCS=""

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
echo "Executable location: $SCRIPT_DIR/build/$PRESET_NAME/trading_engine_service"
echo
echo "To run the trading engine service:"
echo "  cd $SCRIPT_DIR/build/$PRESET_NAME"
echo "  ./trading_engine_service"
echo
