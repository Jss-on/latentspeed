#!/usr/bin/env bash
echo "Testing Python signer directly..."
echo ""

# Test 1: Check if modules import
echo "Test 1: Checking if eth_account and hyperliquid import..."
python3 -c "from eth_account import Account; from hyperliquid.utils.signing import sign_l1_action; print('✓ Imports successful')" 2>&1

echo ""
echo "Test 2: Running signer bridge with ping..."
echo '{"id":1,"method":"ping","params":{}}' | python3 tools/hl_signer_bridge.py 2>&1

echo ""
echo "Test 3: Setting PYTHONPATH and testing..."
export PYTHONPATH="/root/.local/lib/python3.10/site-packages:$PYTHONPATH"
echo '{"id":1,"method":"ping","params":{}}' | python3 tools/hl_signer_bridge.py 2>&1
