#!/usr/bin/env python3
"""
Simple dYdX Streaming Test
Just checks if dYdX data is coming through ZMQ
"""

import zmq
import json
import time

def test_dydx():
    print("🧪 Testing dYdX streaming...\n")
    
    # Connect to ZMQ
    ctx = zmq.Context()
    sock = ctx.socket(zmq.SUB)
    sock.connect("tcp://localhost:5556")
    sock.setsockopt_string(zmq.SUBSCRIBE, "DYDX-")
    sock.setsockopt(zmq.RCVTIMEO, 10000)  # 10 second timeout
    
    print("📡 Listening for dYdX messages...")
    print("   (waiting up to 10 seconds)\n")
    
    try:
        # Get one message
        topic = sock.recv_string()
        message = sock.recv_string()
        data = json.loads(message)
        
        # Print it
        print(f"✅ SUCCESS! Received dYdX data:")
        print(f"   Exchange: {data.get('exchange')}")
        print(f"   Symbol: {data.get('symbol')}")
        print(f"   Price: ${data.get('price', 0):,.2f}")
        print(f"   Side: {data.get('side')}")
        print(f"\n✅ dYdX is working!")
        return True
        
    except zmq.Again:
        print("❌ TIMEOUT - No dYdX messages received")
        print("\nTroubleshooting:")
        print("  1. Is the provider running?")
        print("  2. Run: ./test_market_data dydx BTC-USD")
        return False
        
    except Exception as e:
        print(f"❌ ERROR: {e}")
        return False
    
    finally:
        sock.close()
        ctx.term()

if __name__ == '__main__':
    success = test_dydx()
    exit(0 if success else 1)
