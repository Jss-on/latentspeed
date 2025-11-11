#!/bin/bash
# Setup Python virtual environment for trading strategies

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}================================${NC}"
echo -e "${GREEN}Strategy Environment Setup${NC}"
echo -e "${GREEN}================================${NC}"

# Check Python version
PYTHON_VERSION=$(python3 --version 2>&1 | awk '{print $2}')
echo "Python version: $PYTHON_VERSION"

# Create virtual environment
if [ -d ".venv" ]; then
    echo -e "${YELLOW}Virtual environment already exists. Recreating...${NC}"
    rm -rf .venv
fi

echo "Creating virtual environment..."
python3 -m venv .venv

# Activate
source .venv/bin/activate

# Upgrade pip
echo "Upgrading pip..."
pip install --upgrade pip

# Install required packages
echo "Installing dependencies..."
pip install pyzmq

# Create requirements.txt for reference
cat > .venv/requirements.txt << EOF
# Python dependencies for trading strategies
pyzmq>=25.0.0
EOF

echo -e "\n${GREEN}================================${NC}"
echo -e "${GREEN}Setup Complete!${NC}"
echo -e "${GREEN}================================${NC}"
echo ""
echo "Virtual environment created at: .venv/"
echo "Dependencies installed:"
echo "  - pyzmq (ZeroMQ Python bindings)"
echo ""
echo "To activate manually:"
echo "  source .venv/bin/activate"
echo ""
echo "To run the strategy:"
echo "  ./examples/run_reactive_trading.sh"
echo ""
