#!/usr/bin/env python3
"""
Test the Hyperliquid Signer Bridge
Tests signing functionality independently
"""

import subprocess
import json
import sys

def test_signer():
    print("=" * 70)
    print("Hyperliquid Signer Bridge Test")
    print("=" * 70 + "\n")
    
    # Test credentials (from your config)
    private_key = "0x2e5aacf85446088b3121eec4eab06beda234decc4d16ffe3cb0d2a5ec25ea60b"
    
    # Sample order action
    action = {
        "type": "order",
        "orders": [{
            "a": 0,  # asset index for ETH
            "b": True,  # is_buy
            "p": "3000",  # price
            "s": "0.01",  # size
            "r": False,  # reduce_only
            "t": {"limit": {"tif": "Gtc"}}  # time_in_force
        }],
        "grouping": "na"
    }
    
    print("Starting signer bridge subprocess...")
    
    # Determine script path relative to this test script
    import os
    script_dir = os.path.dirname(os.path.abspath(__file__))
    signer_script = os.path.join(script_dir, "hl_signer_bridge.py")
    
    print(f"   Script path: {signer_script}")
    
    proc = subprocess.Popen(
        ["python3", "-u", signer_script],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1
    )
    
    try:
        # Test 1: Ping
        print("\n1. Testing ping...")
        ping_req = {"id": 1, "method": "ping", "params": {}}
        proc.stdin.write(json.dumps(ping_req) + "\n")
        proc.stdin.flush()
        
        response = proc.stdout.readline()
        print(f"   Request: {ping_req}")
        print(f"   Response: {response.strip()}")
        
        resp_data = json.loads(response)
        if resp_data.get("result") == "pong":
            print("   ✓ Ping successful\n")
        else:
            print(f"   ✗ Ping failed: {resp_data}\n")
            return False
        
        # Test 2: Sign L1 Action
        print("2. Testing sign_l1_action...")
        sign_req = {
            "id": 2,
            "method": "sign_l1",
            "params": {
                "privateKey": private_key,
                "action": action,
                "nonce": 12345,
                "vaultAddress": None,
                "expiresAfter": None,
                "isMainnet": False  # testnet
            }
        }
        
        print(f"   Request: {json.dumps(sign_req, indent=2)}")
        proc.stdin.write(json.dumps(sign_req) + "\n")
        proc.stdin.flush()
        
        response = proc.stdout.readline()
        print(f"   Response: {response.strip()}\n")
        
        resp_data = json.loads(response)
        if "result" in resp_data:
            sig = resp_data["result"]
            print("   ✓ Signature generated successfully!")
            print(f"   r: {sig.get('r', 'N/A')[:20]}...")
            print(f"   s: {sig.get('s', 'N/A')[:20]}...")
            print(f"   v: {sig.get('v', 'N/A')}\n")
            return True
        else:
            print(f"   ✗ Signature failed: {resp_data.get('error', 'Unknown error')}\n")
            return False
            
    except Exception as e:
        print(f"\n✗ Test failed with exception: {e}")
        import traceback
        traceback.print_exc()
        return False
    finally:
        proc.terminate()
        proc.wait()
        stderr_output = proc.stderr.read()
        if stderr_output:
            print(f"\nStderr output:\n{stderr_output}")

def check_dependencies():
    """Check if required packages are installed"""
    print("Checking dependencies...\n")
    
    try:
        import eth_account
        print(f"✓ eth-account: {eth_account.__version__}")
    except ImportError as e:
        print(f"✗ eth-account not installed: {e}")
        return False
    
    try:
        import hyperliquid.utils.signing
        print(f"✓ hyperliquid-python-sdk installed")
    except ImportError as e:
        print(f"✗ hyperliquid-python-sdk not installed: {e}")
        return False
    
    print()
    return True

if __name__ == "__main__":
    if not check_dependencies():
        print("\n" + "=" * 70)
        print("Install dependencies with:")
        print("  pip install -r tools/requirements.txt")
        print("=" * 70)
        sys.exit(1)
    
    success = test_signer()
    
    print("=" * 70)
    if success:
        print("✓ All tests passed!")
        print("The signer bridge is working correctly.")
    else:
        print("✗ Tests failed")
        print("Check error messages above for details.")
    print("=" * 70)
    
    sys.exit(0 if success else 1)
