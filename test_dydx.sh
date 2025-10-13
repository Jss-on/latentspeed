#!/bin/bash
# dYdX Streaming Test Script
# Usage: ./test_dydx.sh [build|run|verify|all]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build/release"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
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
    echo -e "${BLUE}ℹ️  $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
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
        ls -lh multi_exchange_provider test_market_data
    else
        print_error "Build failed - multi_exchange_provider not found"
        exit 1
    fi
}

# Test 2: Create dYdX config
create_config() {
    print_header "Creating dYdX Test Configuration"
    
    cat > "$SCRIPT_DIR/config_test_dydx.yml" << 'EOF'
zmq:
  trades_port: 5556
  books_port: 5557
  window_size: 20
  depth_levels: 10

feeds:
  - exchange: dydx
    symbols:
      - BTC-USD
      - ETH-USD
      - SOL-USD
    enable_trades: true
    enable_orderbook: true
    snapshots_only: true
    snapshot_interval: 1
EOF
    
    print_success "Config created: config_test_dydx.yml"
    print_info "Symbols: BTC-USD, ETH-USD, SOL-USD (dYdX uses USD not USDT)"
}

# Test 3: Run provider (simple mode)
test_run_simple() {
    print_header "Test 2: Running dYdX Provider (Simple Mode)"
    
    if [ ! -f "$BUILD_DIR/test_market_data" ]; then
        print_error "test_market_data not found. Run: $0 build"
        exit 1
    fi
    
    print_info "Starting test_market_data with dYdX..."
    print_info "Symbols: BTC-USD, ETH-USD"
    print_info "Press Ctrl+C to stop"
    echo ""
    
    cd "$BUILD_DIR"
    ./test_market_data dydx BTC-USD,ETH-USD
}

# Test 4: Run provider (multi-exchange mode)
test_run() {
    print_header "Test 2: Running dYdX Provider (Config Mode)"
    
    if [ ! -f "$BUILD_DIR/multi_exchange_provider" ]; then
        print_error "multi_exchange_provider not found. Run: $0 build"
        exit 1
    fi
    
    create_config
    
    print_info "Starting provider..."
    print_info "ZMQ Ports: 5556 (trades), 5557 (orderbooks)"
    print_info "Press Ctrl+C to stop"
    echo ""
    
    cd "$BUILD_DIR"
    ./multi_exchange_provider --config ../../config_test_dydx.yml
}

# Test 5: Verify ZMQ messages
test_verify() {
    print_header "Test 3: Verifying dYdX ZMQ Messages"
    
    print_info "Checking if provider is running..."
    if ! pgrep -f "multi_exchange_provider\|test_market_data" > /dev/null; then
        print_warning "Provider not running. Start it with: $0 run"
        print_info "Starting provider in background..."
        
        # Build if needed
        if [ ! -f "$BUILD_DIR/test_market_data" ]; then
            test_build
        fi
        
        # Start provider in background
        cd "$BUILD_DIR"
        nohup ./test_market_data dydx BTC-USD,ETH-USD > /tmp/dydx_test.log 2>&1 &
        PROVIDER_PID=$!
        
        print_info "Provider started (PID: $PROVIDER_PID)"
        print_info "Waiting 5 seconds for connection..."
        sleep 5
    fi
    
    print_info "Subscribing to dYdX trades for 15 seconds..."
    echo ""
    
    timeout 15 python3 << 'EOF' || true
import zmq
import json
import sys
from datetime import datetime

try:
    ctx = zmq.Context()
    sock = ctx.socket(zmq.SUB)
    sock.connect("tcp://127.0.0.1:5556")
    
    # Subscribe to DYDX topics
    sock.setsockopt_string(zmq.SUBSCRIBE, "DYDX-")
    sock.setsockopt(zmq.RCVTIMEO, 15000)  # 15 second timeout
    
    print("📊 Listening for dYdX trades...")
    print("-" * 80)
    count = 0
    exchanges_seen = set()
    symbols_seen = set()
    
    while count < 10:  # Get at least 10 messages
        try:
            topic = sock.recv_string()
            message = sock.recv_string()
            data = json.loads(message)
            
            count += 1
            exchanges_seen.add(data.get('exchange', 'N/A'))
            symbols_seen.add(data.get('symbol', 'N/A'))
            
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            print(f"\n[{count}] {timestamp} - {topic}")
            print(f"    Exchange: {data.get('exchange', 'N/A')}")
            print(f"    Symbol: {data.get('symbol', 'N/A')}")
            print(f"    Price: {data.get('price', 0):.2f}")
            print(f"    Amount: {data.get('amount', 0):.8f}")
            print(f"    Side: {data.get('side', 'N/A')}")
            
            if 'transaction_price' in data:
                print(f"    Transaction Price: {data['transaction_price']:.2f}")
            if 'volatility_transaction_price' in data:
                print(f"    Volatility: {data['volatility_transaction_price']:.6f}")
            
        except zmq.Again:
            print("\n⏱️  Timeout waiting for messages")
            print("    Possible issues:")
            print("    1. Provider not running")
            print("    2. No active trading on dYdX")
            print("    3. Connection issue to dYdX")
            sys.exit(1)
        except KeyboardInterrupt:
            break
    
    print("\n" + "=" * 80)
    print(f"✅ Received {count} valid dYdX messages")
    print(f"   Exchanges: {', '.join(exchanges_seen)}")
    print(f"   Symbols: {', '.join(symbols_seen)}")
    print("=" * 80)
    
except Exception as e:
    print(f"\n❌ Error: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)
EOF
    
    if [ $? -eq 0 ]; then
        print_success "dYdX ZMQ verification passed"
    else
        print_error "dYdX ZMQ verification failed"
        print_info "Check logs: tail -f /tmp/dydx_test.log"
        exit 1
    fi
}

# Test 6: Check orderbook data
test_orderbook() {
    print_header "Test 4: Verifying dYdX OrderBook Data"
    
    print_info "Checking orderbook messages..."
    
    timeout 15 python3 << 'EOF' || true
import zmq
import json
import sys
from datetime import datetime

try:
    ctx = zmq.Context()
    sock = ctx.socket(zmq.SUB)
    sock.connect("tcp://127.0.0.1:5557")
    
    # Subscribe to DYDX orderbook
    sock.setsockopt_string(zmq.SUBSCRIBE, "DYDX-")
    sock.setsockopt(zmq.RCVTIMEO, 15000)
    
    print("📈 Listening for dYdX orderbook updates...")
    print("-" * 80)
    count = 0
    
    while count < 5:  # Get at least 5 orderbook updates
        try:
            topic = sock.recv_string()
            message = sock.recv_string()
            data = json.loads(message)
            
            count += 1
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            
            print(f"\n[{count}] {timestamp} - {topic}")
            print(f"    Exchange: {data.get('exchange', 'N/A')}")
            print(f"    Symbol: {data.get('symbol', 'N/A')}")
            
            # Best bid/ask
            if data.get('bids') and len(data['bids']) > 0:
                best_bid = data['bids'][0]
                print(f"    Best Bid: {best_bid.get('price', 0):.2f} @ {best_bid.get('quantity', 0):.8f}")
            
            if data.get('asks') and len(data['asks']) > 0:
                best_ask = data['asks'][0]
                print(f"    Best Ask: {best_ask.get('price', 0):.2f} @ {best_ask.get('quantity', 0):.8f}")
            
            # Preprocessed metrics
            if 'midpoint' in data:
                print(f"    Midpoint: {data['midpoint']:.2f}")
            if 'relative_spread' in data:
                print(f"    Spread: {data['relative_spread'] * 10000:.2f} bps")
            if 'imbalance_lvl1' in data:
                print(f"    Imbalance: {data['imbalance_lvl1']:.4f}")
            if 'ofi_rolling' in data:
                print(f"    OFI: {data['ofi_rolling']:.4f}")
            
        except zmq.Again:
            print("\n⏱️  Timeout waiting for orderbook updates")
            sys.exit(1)
        except KeyboardInterrupt:
            break
    
    print("\n" + "=" * 80)
    print(f"✅ Received {count} valid orderbook updates")
    print("=" * 80)
    
except Exception as e:
    print(f"\n❌ Error: {e}")
    sys.exit(1)
EOF
    
    if [ $? -eq 0 ]; then
        print_success "dYdX orderbook verification passed"
    else
        print_error "dYdX orderbook verification failed"
        exit 1
    fi
}

# Test 7: Connection test (WebSocket)
test_connection() {
    print_header "Test 5: Testing dYdX WebSocket Connection"
    
    print_info "Testing connection to wss://indexer.dydx.trade/v4/ws"
    
    python3 << 'EOF'
import asyncio
import websockets
import json

async def test_dydx_connection():
    uri = "wss://indexer.dydx.trade/v4/ws"
    
    try:
        print("Connecting to dYdX...")
        async with websockets.connect(uri) as websocket:
            print("✅ Connected successfully!")
            
            # Subscribe to BTC-USD trades
            subscribe_msg = {
                "type": "subscribe",
                "channel": "v4_trades",
                "id": "BTC-USD",
                "batched": True
            }
            
            await websocket.send(json.dumps(subscribe_msg))
            print("📤 Sent subscription for BTC-USD trades")
            
            # Wait for confirmation
            response = await asyncio.wait_for(websocket.recv(), timeout=10.0)
            data = json.loads(response)
            
            print(f"📥 Received: {data.get('type', 'unknown')}")
            
            if data.get('type') == 'subscribed':
                print("✅ Subscription confirmed")
                print(f"   Channel: {data.get('channel')}")
                print(f"   ID: {data.get('id')}")
                return True
            else:
                print("⚠️  Unexpected response")
                return False
                
    except asyncio.TimeoutError:
        print("❌ Timeout waiting for response")
        return False
    except Exception as e:
        print(f"❌ Connection failed: {e}")
        return False

result = asyncio.run(test_dydx_connection())
exit(0 if result else 1)
EOF
    
    if [ $? -eq 0 ]; then
        print_success "dYdX connection test passed"
    else
        print_error "dYdX connection test failed"
        print_info "Check network connectivity and dYdX status"
        exit 1
    fi
}

# Test all
test_all() {
    print_header "Running All dYdX Tests"
    
    echo ""
    test_build
    echo ""
    
    create_config
    echo ""
    
    print_info "Test suite complete!"
    print_info ""
    print_info "Next steps:"
    print_info "  1. Start provider: $0 run"
    print_info "  2. Verify data (in another terminal): $0 verify"
    print_info "  3. Check orderbooks: $0 orderbook"
    print_info "  4. Test connection: $0 connection"
}

# Show usage
usage() {
    echo "dYdX Streaming Test Script"
    echo ""
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  build       - Build the project"
    echo "  run         - Run multi-exchange provider with dYdX (config mode)"
    echo "  simple      - Run test_market_data with dYdX (simple mode)"
    echo "  verify      - Verify dYdX trade messages (requires provider running)"
    echo "  orderbook   - Verify dYdX orderbook messages"
    echo "  connection  - Test WebSocket connection to dYdX"
    echo "  all         - Build and create config (default)"
    echo ""
    echo "Examples:"
    echo "  # Full test workflow"
    echo "  $0 build                    # Build binaries"
    echo "  $0 run                      # Terminal 1: Start provider"
    echo "  $0 verify                   # Terminal 2: Verify streaming"
    echo "  $0 orderbook                # Terminal 2: Check orderbooks"
    echo ""
    echo "  # Quick test"
    echo "  $0 simple                   # Run simple mode"
    echo ""
    echo "  # Connection test"
    echo "  $0 connection               # Test dYdX WebSocket"
}

# Main
case "${1:-all}" in
    build)
        test_build
        ;;
    run)
        test_run
        ;;
    simple)
        test_run_simple
        ;;
    verify)
        test_verify
        ;;
    orderbook)
        test_orderbook
        ;;
    connection)
        test_connection
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
