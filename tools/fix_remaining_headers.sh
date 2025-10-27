#!/bin/bash

# Script to batch-fix remaining headers with implementations
# Usage: ./fix_remaining_headers.sh

echo "🔧 Header Implementation Cleanup Script"
echo "========================================="
echo ""

HEADERS_TO_FIX=(
    "hyperliquid_perpetual_connector.h"
    "hyperliquid_user_stream_data_source.h"
    "hyperliquid_order_book_data_source.h"
    "zmq_order_event_publisher.h"
    "client_order_tracker.h"
    "rolling_stats.h"
    "position.h"
    "in_flight_order.h"
    "order_book.h"
    "trading_rule.h"
    "hyperliquid_auth.h"
    "hyperliquid_marketstream_adapter.h"
    "trading_engine_service.h"
)

INCLUDE_DIR="include/connector"
SRC_DIR="src/connector"

echo "📋 Files to process:"
for header in "${HEADERS_TO_FIX[@]}"; do
    echo "   - $header"
done
echo ""

echo "⚠️  Note: This script generates templates only."
echo "   Manual implementation of methods still required."
echo ""

for header in "${HEADERS_TO_FIX[@]}"; do
    HEADER_FILE="$INCLUDE_DIR/$header"
    CPP_FILE="$SRC_DIR/${header%.h}.cpp"
    
    if [ ! -f "$HEADER_FILE" ]; then
        echo "❌ Skipping $header (not found in $INCLUDE_DIR)"
        continue
    fi
    
    if [ -f "$CPP_FILE" ]; then
        echo "⏭️  Skipping $header (cpp already exists)"
        continue
    fi
    
    echo "📝 Creating $CPP_FILE template..."
    
    # Extract namespace and class name from header
    CLASS_NAME=$(grep -oP 'class\s+\K\w+' "$HEADER_FILE" | head -1)
    
    # Create template cpp file
    cat > "$CPP_FILE" << EOF
/**
 * @file ${header%.h}.cpp
 * @brief Implementation of ${CLASS_NAME}
 */

#include "connector/$header"
#include <spdlog/spdlog.h>

namespace latentspeed::connector {

// TODO: Move all method implementations from $header to this file
// 
// Steps:
// 1. Copy each method implementation from the header
// 2. Replace inline implementation with declaration in header
// 3. Ensure all includes are present
// 4. Test compilation

// Example:
// ClassName::MethodName() {
//     // implementation
// }

} // namespace latentspeed::connector
EOF
    
    echo "✅ Created $CPP_FILE template"
done

echo ""
echo "✨ Done! Next steps:"
echo "   1. Review generated .cpp files"
echo "   2. Move implementations from headers"
echo "   3. Update CMakeLists.txt to include new .cpp files"
echo "   4. Compile and test"
