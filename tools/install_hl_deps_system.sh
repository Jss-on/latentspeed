#!/usr/bin/env bash
set -euo pipefail

echo "Installing Hyperliquid signer dependencies system-wide..."
echo ""

# Install to system Python
sudo python3 -m pip install --upgrade pip
sudo python3 -m pip install eth-account>=0.11.0 hyperliquid-python-sdk>=0.4.0

echo ""
echo "Verifying installation..."
python3 -c "import eth_account; print('✓ eth_account:', eth_account.__version__)"
python3 -c "import hyperliquid; print('✓ hyperliquid-python-sdk installed')"

echo ""
echo "[OK] Dependencies installed successfully!"
echo "Restart your trading engine with: ./run.sh --release"
