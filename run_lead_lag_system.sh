#!/bin/bash
# Complete Lead-Lag Trading System Orchestration
# Runs: MarketStream → Lead-Lag Strategy → Trading Engine
# For Hyperliquid perpetuals

set -e

# Check and cleanup ports
cleanup_ports() {
    local ports=("$@")
    for port in "${ports[@]}"; do
        local pids=$(lsof -ti:$port 2>/dev/null || true)
        if [ ! -z "$pids" ]; then
            echo -e "${YELLOW}  Port $port is in use by PID(s): $pids${NC}"
            echo -e "${BLUE}  Killing processes on port $port...${NC}"
            echo "$pids" | xargs -r kill -9 2>/dev/null || true
            sleep 1
        fi
    done
}

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}🛑 Shutting down system...${NC}"
    
    # Stop Python strategy
    if [ ! -z "$STRATEGY_PID" ] && kill -0 $STRATEGY_PID 2>/dev/null; then
        echo -e "${BLUE}  Stopping lead-lag strategy (PID: $STRATEGY_PID)${NC}"
        kill -SIGTERM $STRATEGY_PID 2>/dev/null || true
        wait $STRATEGY_PID 2>/dev/null || true
    fi
    
    # Stop trading engine
    if [ ! -z "$TRADING_ENGINE_PID" ] && kill -0 $TRADING_ENGINE_PID 2>/dev/null; then
        echo -e "${BLUE}  Stopping trading engine (PID: $TRADING_ENGINE_PID)${NC}"
        kill -SIGTERM $TRADING_ENGINE_PID 2>/dev/null || true
        wait $TRADING_ENGINE_PID 2>/dev/null || true
    fi
    
    # Stop marketstream
    if [ ! -z "$MARKETSTREAM_PID" ] && kill -0 $MARKETSTREAM_PID 2>/dev/null; then
        echo -e "${BLUE}  Stopping marketstream (PID: $MARKETSTREAM_PID)${NC}"
        kill -SIGTERM $MARKETSTREAM_PID 2>/dev/null || true
        wait $MARKETSTREAM_PID 2>/dev/null || true
    fi
    
    echo -e "${GREEN}✓ System stopped${NC}"
    exit 0
}
