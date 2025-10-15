#!/usr/bin/env python3
"""
Simple test script to verify marketstream ZMQ connection
Minimal dependencies - just prints raw data
"""

import zmq
import json
import sys

def test_zmq_connection():
    """Test basic ZMQ connection and print incoming data"""
    
    print("="*70)
    print("Simple MarketStream ZMQ Test")
    print("="*70)
    print("Connecting to ZMQ streams...")
    
    context = zmq.Context()
    
    # Connect to trades
    trades_socket = context.socket(zmq.SUB)
    trades_socket.connect("tcp://127.0.0.1:5556")
    trades_socket.setsockopt_string(zmq.SUBSCRIBE, "")
    trades_socket.setsockopt(zmq.RCVTIMEO, 1000)  # 1 second timeout
    print("✓ Connected to trades: tcp://127.0.0.1:5556")
    
    # Connect to orderbooks
    books_socket = context.socket(zmq.SUB)
    books_socket.connect("tcp://127.0.0.1:5557")
    books_socket.setsockopt_string(zmq.SUBSCRIBE, "")
    books_socket.setsockopt(zmq.RCVTIMEO, 1000)  # 1 second timeout
    print("✓ Connected to orderbooks: tcp://127.0.0.1:5557")
    
    print("\nListening for data (Ctrl+C to stop)...\n")
    
    trade_count = 0
    book_count = 0
    
    try:
        while True:
            # Check trades (multi-part message: topic + data)
            try:
                topic = trades_socket.recv_string(flags=zmq.NOBLOCK)
                trade = trades_socket.recv_json()
                trade_count += 1
                print(f"[TRADE #{trade_count}] {trade.get('exchange')}:{trade.get('symbol')} - "
                      f"Price: ${trade.get('price', 0):.2f}, "
                      f"Amount: {trade.get('amount', 0):.4f}, "
                      f"Side: {trade.get('side', 'N/A')}")
            except zmq.Again:
                pass
            except Exception as e:
                print(f"⚠ Error receiving trade: {e}")
            
            # Check orderbooks (multi-part message: topic + data)
            try:
                topic = books_socket.recv_string(flags=zmq.NOBLOCK)
                book = books_socket.recv_json()
                book_count += 1
                
                # Note: bids/asks are arrays [[price, qty], [price, qty], ...]
                bids = book.get('bids', [])
                asks = book.get('asks', [])
                best_bid = bids[0][0] if bids and len(bids[0]) >= 1 else 0
                best_ask = asks[0][0] if asks and len(asks[0]) >= 1 else 0
                mid = (best_bid + best_ask) / 2 if best_bid and best_ask else 0
                
                print(f"[BOOK #{book_count}] {book.get('exchange')}:{book.get('symbol')} - "
                      f"Mid: ${mid:.2f}, "
                      f"Bid: ${best_bid:.2f}, "
                      f"Ask: ${best_ask:.2f}")
            except zmq.Again:
                pass
            except Exception as e:
                print(f"⚠ Error receiving book: {e}")
    
    except KeyboardInterrupt:
        print(f"\n\nReceived {trade_count} trades and {book_count} orderbooks")
        print("Test complete!")
    
    finally:
        trades_socket.close()
        books_socket.close()
        context.term()

if __name__ == "__main__":
    try:
        test_zmq_connection()
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
