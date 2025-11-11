#!/usr/bin/env python3
"""
Quick test script to verify marketstream ZMQ messages
"""

import zmq
import json
import time

def test_marketstream():
    context = zmq.Context()
    
    # Subscribe to trades
    trades_sub = context.socket(zmq.SUB)
    trades_sub.connect("tcp://127.0.0.1:5556")
    trades_sub.setsockopt_string(zmq.SUBSCRIBE, "")  # Subscribe to all topics
    trades_sub.setsockopt(zmq.RCVTIMEO, 10000)  # 10 second timeout
    
    # Subscribe to orderbooks
    books_sub = context.socket(zmq.SUB)
    books_sub.connect("tcp://127.0.0.1:5557")
    books_sub.setsockopt_string(zmq.SUBSCRIBE, "")
    books_sub.setsockopt(zmq.RCVTIMEO, 10000)
    
    print("Listening for marketstream messages...")
    print("Press Ctrl+C to stop\n")
    
    trades_count = 0
    books_count = 0
    
    try:
        while True:
            # Check for trades
            try:
                topic = trades_sub.recv_string(flags=zmq.NOBLOCK)
                data = trades_sub.recv_json()
                trades_count += 1
                print(f"[TRADE #{trades_count}] Topic: {topic}")
                print(f"  Exchange: {data.get('exchange', 'N/A')}")
                print(f"  Symbol: {data.get('symbol', 'N/A')}")
                print(f"  Price: ${data.get('price', 0):.2f}")
                print(f"  Amount: {data.get('amount', 0):.4f}")
                print(f"  Side: {data.get('side', 'N/A')}")
                print(f"  Raw data: {json.dumps(data, indent=2)}\n")
            except zmq.error.Again:
                pass
            
            # Check for orderbooks
            try:
                topic = books_sub.recv_string(flags=zmq.NOBLOCK)
                data = books_sub.recv_json()
                books_count += 1
                print(f"[BOOK #{books_count}] Topic: {topic}")
                print(f"  Exchange: {data.get('exchange', 'N/A')}")
                print(f"  Symbol: {data.get('symbol', 'N/A')}")
                print(f"  Midpoint: ${data.get('midpoint', 0):.2f}")
                print(f"  Spread: {data.get('relative_spread', 0) * 10000:.2f} bps\n")
            except zmq.error.Again:
                pass
            
            time.sleep(0.01)  # Small delay to prevent busy loop
            
    except KeyboardInterrupt:
        print(f"\n\nReceived {trades_count} trades and {books_count} orderbook updates")
    finally:
        trades_sub.close()
        books_sub.close()
        context.term()
        print("Cleanup complete")

if __name__ == "__main__":
    test_marketstream()
