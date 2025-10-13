#!/bin/bash
# Quick Bybit Testing Script
# Usage: ./test_bybit.sh [build|run|verify|all]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build/release"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}$1${NC}"
    echo -e "${GREEN}========================================${NC}"
}

print_error() {
    echo -e "${RED}❌ ERROR: $1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_info() {
    echo -e "${YELLOW}ℹ️  $1${NC}"
}

# Test 1: Build
test_build() {
    print_header "Test 1: Building Project"
    
    cd "$SCRIPT_DIR"
    
    if [ ! -d "build/release" ]; then
        print_info "Creating build directory..."
        mkdir -p build/release
    fi
    
    cd build/release
    
    print_info "Running CMake..."
    cmake ../.. -DCMAKE_BUILD_TYPE=Release
    
    print_info "Building with Ninja..."
    ninja
    
    if [ -f "multi_exchange_provider" ]; then
        print_success "Build completed successfully"
        ls -lh multi_exchange_provider trading_engine_service test_market_data
    else
        print_error "Build failed - multi_exchange_provider not found"
        exit 1
    fi
}

# Test 2: Create config
create_config() {
    print_header "Creating Test Configuration"
    
    cat > "$SCRIPT_DIR/config_test_bybit.yml" << 'EOF'
zmq:
  trades_port: 5556
  books_port: 5557
  window_size: 20
  depth_levels: 10

feeds:
  - exchange: bybit
    symbols:
      - BTC-USDT
      - ETH-USDT
    enable_trades: true
    enable_orderbook: true
    snapshots_only: true
    snapshot_interval: 1
EOF
    
    print_success "Config created: config_test_bybit.yml"
}

# Test 3: Run provider
test_run() {
    print_header "Test 2: Running Multi-Exchange Provider"
    
    if [ ! -f "$BUILD_DIR/multi_exchange_provider" ]; then
        print_error "multi_exchange_provider not found. Run: $0 build"
        exit 1
    fi
    
    create_config
    
    print_info "Starting provider..."
    print_info "Press Ctrl+C to stop"
    echo ""
    
    cd "$BUILD_DIR"
    ./multi_exchange_provider --config ../../config_test_bybit.yml
}

# Test 4: Verify ZMQ
test_verify() {
    print_header "Test 3: Verifying ZMQ Messages"
    
    print_info "Checking if provider is running..."
    if ! pgrep -f multi_exchange_provider > /dev/null; then
        print_error "Provider not running. Start it with: $0 run"
        exit 1
    fi
    
    print_info "Subscribing to Bybit trades for 10 seconds..."
    
    timeout 10 python3 << 'EOF' || true
import zmq
import json
import sys

try:
    ctx = zmq.Context()
    sock = ctx.socket(zmq.SUB)
    sock.connect("tcp://127.0.0.1:5556")
    sock.setsockopt_string(zmq.SUBSCRIBE, "BYBIT-")
    sock.setsockopt(zmq.RCVTIMEO, 10000)  # 10 second timeout
    
    print("\n📊 Listening for Bybit trades...")
    count = 0
    
    while count < 5:
        try:
            topic = sock.recv_string()
            message = sock.recv_string()
            data = json.loads(message)
            
            count += 1
            print(f"\n[{count}] {data['exchange']} {data['symbol']}")
            print(f"    Price: {data['price']}")
            print(f"    Amount: {data['amount']}")
            print(f"    Side: {data['side']}")
            
        except zmq.Again:
            print("\n⏱️  Timeout waiting for messages")
            print("    Is the provider running?")
            sys.exit(1)
    
    print(f"\n✅ Received {count} valid messages")
    
except Exception as e:
    print(f"\n❌ Error: {e}")
    sys.exit(1)
EOF
    
    if [ $? -eq 0 ]; then
        print_success "ZMQ verification passed"
    else
        print_error "ZMQ verification failed"
        exit 1
    fi
}

# Test all
test_all() {
    print_header "Running All Tests"
    
    test_build
    echo ""
    
    create_config
    echo ""
    
    print_info "Test suite complete!"
    print_info "To run the provider: $0 run"
    print_info "To verify ZMQ: $0 verify (in another terminal while provider runs)"
}

# Show usage
usage() {
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  build   - Build the project"
    echo "  run     - Run multi-exchange provider with Bybit"
    echo "  verify  - Verify ZMQ messages (requires provider running)"
    echo "  all     - Build and create config (default)"
    echo ""
    echo "Examples:"
    echo "  $0 build"
    echo "  $0 run"
    echo "  $0 verify  # In another terminal"
}

# Main
case "${1:-all}" in
    build)
        test_build
        ;;
    run)
        test_run
        ;;
    verify)
        test_verify
        ;;
    all)
        test_all
        ;;
    help|--help|-h)
        usage
        ;;
    *)
        print_error "Unknown command: $1"
        usage
        exit 1
        ;;
esac
