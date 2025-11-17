#!/usr/bin/env bash
echo "=== Python Diagnostic ==="
echo ""
echo "1. Which python3:"
which python3
echo ""
echo "2. Python3 version:"
python3 --version
echo ""
echo "3. Python3 path:"
python3 -c "import sys; print(sys.executable)"
echo ""
echo "4. Check eth_account module:"
python3 -c "import eth_account; print('✓ eth_account installed'); print(eth_account.__file__)" 2>&1 || echo "✗ eth_account NOT installed"
echo ""
echo "5. Check hyperliquid module:"
python3 -c "import hyperliquid; print('✓ hyperliquid installed'); print(hyperliquid.__file__)" 2>&1 || echo "✗ hyperliquid NOT installed"
echo ""
echo "6. Python site-packages:"
python3 -c "import site; print(site.getsitepackages())"
echo ""
echo "7. Test the signer script directly:"
echo '{"id":1,"method":"ping","params":{}}' | python3 tools/hl_signer_bridge.py
