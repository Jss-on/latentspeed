#!/usr/bin/env python3

"""
ZMQ Subscriber Test for Market Data Streams
Tests the market data provider by subscribing to ZMQ ports 5556 and 5557
"""

import zmq
import json
import time
import threading
import argparse
from datetime import datetime

class ZMQSubscriber:
    def __init__(self, port, stream_type):
        self.port = port
        self.stream_type = stream_type
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.SUB)
        self.socket.connect(f"tcp://localhost:{port}")
        self.socket.setsockopt(zmq.SUBSCRIBE, b"")  # Subscribe to all messages
        self.message_count = 0
        self.running = False
        
    def start(self):
        self.running = True
        self.thread = threading.Thread(target=self._run)
        self.thread.daemon = True
        self.thread.start()
        
    def stop(self):
        self.running = False
        if hasattr(self, 'thread'):
            self.thread.join(timeout=1.0)
        self.socket.close()
        self.context.term()
        
    def _run(self):
        print(f"[{self.stream_type}] Subscribing to port {self.port}")
        
        while self.running:
            try:
                # Receive multipart message (topic, data)
                topic = self.socket.recv_string(zmq.NOBLOCK)
                data = self.socket.recv_string(zmq.NOBLOCK)
                
                self.message_count += 1
                self._process_message(topic, data)
                
            except zmq.Again:
                # No message available, small sleep
                time.sleep(0.001)
            except Exception as e:
                print(f"[{self.stream_type}] Error: {e}")
                time.sleep(0.1)
                
    def _process_message(self, topic, data):
        try:
            msg = json.loads(data)
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            
            if self.stream_type == "TRADES":
                print(f"[{timestamp}] TRADE {topic}: {msg['price']} x {msg['quantity']} ({msg['side']}) - {msg['exchange']}")
                
            elif self.stream_type == "ORDERBOOK":
                best_bid = msg['bids'][0] if msg['bids'] else [0, 0]
                best_ask = msg['asks'][0] if msg['asks'] else [0, 0]
                print(f"[{timestamp}] BOOK {topic}: {best_bid[0]}@{best_bid[1]} | {best_ask[0]}@{best_ask[1]} - {msg['exchange']}")
                
            # Print every 10th message for trades, every message for orderbook
            if self.stream_type == "ORDERBOOK" or self.message_count % 10 == 0:
                self._print_detailed(msg)
                
        except json.JSONDecodeError:
            print(f"[{self.stream_type}] Invalid JSON: {data[:100]}...")
        except Exception as e:
            print(f"[{self.stream_type}] Process error: {e}")
            
    def _print_detailed(self, msg):
        if self.stream_type == "TRADES":
            print(f"    Trade Details: ID={msg.get('trade_id', 'N/A')}, "
                  f"Timestamp={msg.get('timestamp_ns', 'N/A')}")
        elif self.stream_type == "ORDERBOOK":
            bid_count = len([b for b in msg['bids'] if b[0] > 0])
            ask_count = len([a for a in msg['asks'] if a[0] > 0])
            print(f"    Book Details: {bid_count} bids, {ask_count} asks, "
                  f"Sequence={msg.get('sequence', 'N/A')}")

def main():
    parser = argparse.ArgumentParser(description='Test ZMQ market data streams')
    parser.add_argument('--trades-port', type=int, default=5556,
                        help='Trades stream port (default: 5556)')
    parser.add_argument('--orderbook-port', type=int, default=5557,
                        help='OrderBook stream port (default: 5557)')
    parser.add_argument('--trades-only', action='store_true',
                        help='Subscribe to trades only')
    parser.add_argument('--orderbook-only', action='store_true',
                        help='Subscribe to orderbook only')
    parser.add_argument('--duration', type=int, default=0,
                        help='Test duration in seconds (0 = infinite)')
    
    args = parser.parse_args()
    
    print("=== ZMQ Market Data Subscriber Test ===")
    print(f"Trades Port: {args.trades_port}")
    print(f"OrderBook Port: {args.orderbook_port}")
    print("Press Ctrl+C to stop\n")
    
    subscribers = []
    
    try:
        # Create subscribers
        if not args.orderbook_only:
            trades_sub = ZMQSubscriber(args.trades_port, "TRADES")
            subscribers.append(trades_sub)
            trades_sub.start()
            
        if not args.trades_only:
            orderbook_sub = ZMQSubscriber(args.orderbook_port, "ORDERBOOK")
            subscribers.append(orderbook_sub)
            orderbook_sub.start()
            
        # Statistics reporting
        start_time = time.time()
        
        if args.duration > 0:
            print(f"Running for {args.duration} seconds...")
            time.sleep(args.duration)
        else:
            print("Running indefinitely (Ctrl+C to stop)...")
            while True:
                time.sleep(10)
                
                elapsed = time.time() - start_time
                print(f"\n=== STATS ({elapsed:.1f}s) ===")
                for sub in subscribers:
                    rate = sub.message_count / elapsed if elapsed > 0 else 0
                    print(f"{sub.stream_type}: {sub.message_count} messages ({rate:.1f}/sec)")
                print()
                
    except KeyboardInterrupt:
        print("\nStopping subscribers...")
        
    finally:
        # Cleanup
        for sub in subscribers:
            sub.stop()
            
        # Final stats
        elapsed = time.time() - start_time
        print(f"\n=== FINAL STATS ({elapsed:.1f}s) ===")
        for sub in subscribers:
            rate = sub.message_count / elapsed if elapsed > 0 else 0
            print(f"{sub.stream_type}: {sub.message_count} messages ({rate:.1f}/sec)")
            
        print("Test completed.")

if __name__ == "__main__":
    main()
