#!/bin/bash
# Complete reactive trading system startup script
# Starts: Marketstream → Strategy → Trading Engine

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}================================${NC}"
echo -e "${GREEN}Reactive Trading System Startup${NC}"
echo -e "${GREEN}================================${NC}"

# Check if we're in the right directory
if [ ! -f "run.sh" ]; then
    echo -e "${RED}Error: Please run from latentspeed root directory${NC}"
    exit 1
fi


# Create logs directory
mkdir -p logs

# Export credentials (using your testnet credentials)
export HYPERLIQUID_USER_ADDRESS=0x44Fd91bEd5c87A4fFA222462798BB9d7Ef3669be
export HYPERLIQUID_PRIVATE_KEY=0x2e5aacf85446088b3121eec4eab06beda234decc4d16ffe3cb0d2a5ec25ea60b

echo -e "\n${GREEN}Step 1/3: Starting Marketstream (Market Data Provider)${NC}"
echo "  - Connecting to Hyperliquid testnet"
echo "  - Publishing trades on tcp://127.0.0.1:5556"
echo "  - Publishing orderbooks on tcp://127.0.0.1:5557"

./build/release/marketstream configs/marketstream_hyperliquid.yml > logs/marketstream.log 2>&1 &
MARKETSTREAM_PID=$!
echo "  - PID: $MARKETSTREAM_PID"
sleep 3

echo -e "\n${GREEN}Step 2/3: Starting Trading Engine${NC}"
echo "  - Connecting to Hyperliquid testnet"
echo "  - Accepting orders on tcp://127.0.0.1:5601"
echo "  - Publishing reports on tcp://127.0.0.1:5602"

./build/release/trading_engine_service \
    --exchange hyperliquid \
    --api-key $HYPERLIQUID_USER_ADDRESS \
    --api-secret $HYPERLIQUID_PRIVATE_KEY \
    > logs/trading_engine.log 2>&1 &
ENGINE_PID=$!
echo "  - PID: $ENGINE_PID"
sleep 5

echo -e "\n${GREEN}Step 3/3: Starting Python Strategy${NC}"
echo "  - Symbol: BTC"
echo "  - Strategy: Simple Momentum"
echo "  - Position size: 0.001 BTC"
echo "  - Max position: 0.01 BTC"

# Activate Python virtual environment
if [ -f ".venv/bin/activate" ]; then
    echo "  - Activating .venv environment"
    source .venv/bin/activate
elif [ -f "examples/.venv/bin/activate" ]; then
    echo "  - Activating examples/.venv environment"
    source examples/.venv/bin/activate
else
    echo -e "  - ${YELLOW}Warning: No .venv found, using system Python${NC}"
fi

python3 examples/strategy_simple_momentum.py \
    --symbol BTC \
    --size 0.001 \
    --max-position 0.01 \
    --window 20 \
    --threshold 0.0005 \
    --log-level INFO &
STRATEGY_PID=$!
echo "  - PID: $STRATEGY_PID"

echo -e "\n${GREEN}================================${NC}"
echo -e "${GREEN}All components started!${NC}"
echo -e "${GREEN}================================${NC}"
echo ""
echo "PIDs:"
echo "  - Marketstream: $MARKETSTREAM_PID"
echo "  - Trading Engine: $ENGINE_PID"
echo "  - Strategy: $STRATEGY_PID"
echo ""
echo "Logs:"
echo "  - Marketstream: logs/marketstream.log"
echo "  - Trading Engine: logs/trading_engine.log"
echo "  - Strategy: stdout"
echo ""
echo -e "${YELLOW}Press Ctrl+C to stop all components${NC}"
echo ""

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Shutting down all components...${NC}"
    kill $STRATEGY_PID 2>/dev/null || true
    kill $ENGINE_PID 2>/dev/null || true
    kill $MARKETSTREAM_PID 2>/dev/null || true
    wait
    echo -e "${GREEN}Shutdown complete${NC}"
    exit 0
}

trap cleanup SIGINT SIGTERM

# Wait for strategy process
wait $STRATEGY_PID
