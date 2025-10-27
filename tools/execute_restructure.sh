#!/bin/bash

# Restructure connector files to Hummingbot pattern
# This script moves Hyperliquid-specific files to exchange/hyperliquid/ subdirectory

set -e  # Exit on error

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

echo "🏗️  Restructuring connector directory to Hummingbot pattern"
echo "============================================================"
echo ""

# Ensure directories exist
echo "📁 Ensuring directory structure..."
mkdir -p include/connector/exchange/hyperliquid
mkdir -p src/connector/exchange/hyperliquid
echo "   ✅ Directories created"
echo ""

# Move header files
echo "📦 Moving header files to exchange/hyperliquid/..."

HEADER_FILES=(
    "hyperliquid_auth.h"
    "hyperliquid_integrated_connector.h"
    "hyperliquid_marketstream_adapter.h"
    "hyperliquid_order_book_data_source.h"
    "hyperliquid_perpetual_connector.h"
    "hyperliquid_user_stream_data_source.h"
    "hyperliquid_web_utils.h"
)

for file in "${HEADER_FILES[@]}"; do
    if [ -f "include/connector/$file" ]; then
        git mv "include/connector/$file" "include/connector/exchange/hyperliquid/$file" 2>/dev/null || \
        mv "include/connector/$file" "include/connector/exchange/hyperliquid/$file"
        echo "   ✅ Moved $file"
    else
        echo "   ⚠️  $file not found (may already be moved)"
    fi
done

echo ""
echo "📦 Moving implementation files to exchange/hyperliquid/..."

CPP_FILES=(
    "hyperliquid_auth.cpp"
    "hyperliquid_integrated_connector.cpp"
    "hyperliquid_order_book_data_source.cpp"
    "hyperliquid_perpetual_connector.cpp"
    "hyperliquid_user_stream_data_source.cpp"
)

for file in "${CPP_FILES[@]}"; do
    if [ -f "src/connector/$file" ]; then
        git mv "src/connector/$file" "src/connector/exchange/hyperliquid/$file" 2>/dev/null || \
        mv "src/connector/$file" "src/connector/exchange/hyperliquid/$file"
        echo "   ✅ Moved $file"
    else
        echo "   ⚠️  $file not found (may already be moved)"
    fi
done

echo ""
echo "🔄 Updating include paths in moved files..."

# Update include paths in header files
find include/connector/exchange/hyperliquid -name "*.h" -type f -exec sed -i \
    's|#include "connector/hyperliquid_|#include "connector/exchange/hyperliquid/hyperliquid_|g' {} \;

# Update include paths in implementation files
find src/connector/exchange/hyperliquid -name "*.cpp" -type f -exec sed -i \
    's|#include "connector/hyperliquid_|#include "connector/exchange/hyperliquid/hyperliquid_|g' {} \;

echo "   ✅ Include paths updated in moved files"

echo ""
echo "🔄 Updating include paths in remaining connector files..."

# Update include paths in remaining connector header files
find include/connector -maxdepth 1 -name "*.h" -type f -exec sed -i \
    's|#include "connector/hyperliquid_|#include "connector/exchange/hyperliquid/hyperliquid_|g' {} \;

# Update include paths in remaining connector cpp files
find src/connector -maxdepth 1 -name "*.cpp" -type f -exec sed -i \
    's|#include "connector/hyperliquid_|#include "connector/exchange/hyperliquid/hyperliquid_|g' {} \;

echo "   ✅ Include paths updated in remaining files"

echo ""
echo "🔄 Updating include paths in adapters..."

# Update include paths in adapters directory
if [ -d "include/adapters" ]; then
    find include/adapters -name "*.h" -type f -exec sed -i \
        's|#include "connector/hyperliquid_|#include "connector/exchange/hyperliquid/hyperliquid_|g' {} \;
    echo "   ✅ Updated adapters"
fi

if [ -d "src/adapters" ]; then
    find src/adapters -name "*.cpp" -type f -exec sed -i \
        's|#include "connector/hyperliquid_|#include "connector/exchange/hyperliquid/hyperliquid_|g' {} \;
fi

echo ""
echo "🔄 Updating include paths in examples..."

# Update include paths in examples directory
if [ -d "examples" ]; then
    find examples -name "*.cpp" -type f -exec sed -i \
        's|#include "connector/hyperliquid_|#include "connector/exchange/hyperliquid/hyperliquid_|g' {} \;
    echo "   ✅ Updated examples"
fi

echo ""
echo "✅ Restructuring complete!"
echo ""
echo "📊 Summary:"
echo "   ✅ Moved ${#HEADER_FILES[@]} header files"
echo "   ✅ Moved ${#CPP_FILES[@]} implementation files"
echo "   ✅ Updated include paths in all files"
echo ""
echo "📁 New structure:"
echo "   include/connector/"
echo "   ├── connector_base.h              (core)"
echo "   ├── client_order_tracker.h        (core)"
echo "   ├── in_flight_order.h             (core)"
echo "   └── exchange/"
echo "       └── hyperliquid/"
echo "           ├── hyperliquid_auth.h"
echo "           ├── hyperliquid_integrated_connector.h"
echo "           └── ..."
echo ""
echo "🔄 Next steps:"
echo "   1. Update CMakeLists.txt (see below)"
echo "   2. Test compilation: make -j\$(nproc)"
echo "   3. Commit changes: git add -A && git commit -m 'refactor: restructure to Hummingbot pattern'"
echo ""
echo "📝 CMakeLists.txt update needed:"
echo "=================================================="
cat << 'EOF'

# Connector Core Library
set(CONNECTOR_CORE_SOURCES
    src/connector/connector_base.cpp
    src/connector/client_order_tracker.cpp
    src/connector/in_flight_order.cpp
    src/connector/zmq_order_event_publisher.cpp
)

# Hyperliquid Exchange Implementation
set(HYPERLIQUID_CONNECTOR_SOURCES
    src/connector/exchange/hyperliquid/hyperliquid_auth.cpp
    src/connector/exchange/hyperliquid/hyperliquid_integrated_connector.cpp
    src/connector/exchange/hyperliquid/hyperliquid_perpetual_connector.cpp
    src/connector/exchange/hyperliquid/hyperliquid_user_stream_data_source.cpp
    src/connector/exchange/hyperliquid/hyperliquid_order_book_data_source.cpp
)

# Combined Connector Library
add_library(latentspeed_connector STATIC
    ${CONNECTOR_CORE_SOURCES}
    ${HYPERLIQUID_CONNECTOR_SOURCES}
)

target_include_directories(latentspeed_connector PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

EOF
echo "=================================================="
