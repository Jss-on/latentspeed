#!/usr/bin/env python3
"""Test the hl_signer_bridge directly via subprocess"""

import subprocess
import json
import sys

# Test action
action = {
    "type": "order",
    "grouping": "na",
    "orders": [{
        "a": 4,
        "b": True,
        "p": "1000000.0",
        "s": "0.001",
        "r": False,
        "t": {"limit": {"tif": "Ioc"}},
        "c": "0x42ec8c8340c82aea9bdc2ba2d952cfff"
    }]
}

private_key = "0x2e5aacf85446088b3121eec4eab06beda234decc4d16ffe3cb0d2a5ec25ea60b"
expected_wallet = "0x85c9cF5AEdd0B1C642Ac0023F780Fbb06B94A754"

print("Starting hl_signer_bridge subprocess...")
proc = subprocess.Popen(
    ["python3", "-u", "tools/hl_signer_bridge.py"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
    bufsize=1
)

# Test build_payload
request = {
    "id": 1,
    "method": "build_payload",
    "params": {
        "privateKey": private_key,
        "action": action,
        "nonce": 1763560282262,
        "vaultAddress": None,
        "isMainnet": False
    }
}

print(f"\nSending request:")
print(json.dumps(request, indent=2))
print()

# Send request
proc.stdin.write(json.dumps(request) + "\n")
proc.stdin.flush()

# Read response
response_line = proc.stdout.readline()
print(f"Response: {response_line.strip()}")
print()

# Read stderr
import select
import time
time.sleep(0.1)  # Give it time to write stderr

# Try to read stderr without blocking
try:
    stderr_output = proc.stderr.read()
    if stderr_output:
        print(f"Stderr output:\n{stderr_output}")
except:
    pass

# Parse response
try:
    response = json.loads(response_line)
    if "error" in response:
        print(f"❌ Error: {response['error']}")
        sys.exit(1)
    
    payload = json.loads(response["result"])
    signature = payload["signature"]
    
    print(f"✅ Payload generated successfully")
    print(f"\nSignature:")
    print(f"  r: {signature['r']} ({len(signature['r'])} chars)")
    print(f"  s: {signature['s']} ({len(signature['s'])} chars)")
    print(f"  v: {signature['v']}")
    
    # Verify padding
    if len(signature['r']) == 66 and len(signature['s']) == 66:
        print(f"\n✅ Signature properly padded")
    else:
        print(f"\n❌ Signature NOT properly padded!")
        
except Exception as e:
    print(f"Failed to parse response: {e}")
    sys.exit(1)
finally:
    proc.terminate()

print(f"\nExpected wallet: {expected_wallet}")
print("Now test with the actual Hyperliquid API to see if signature recovery works")
