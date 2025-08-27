#!/usr/bin/env python3
"""
Market Data Monitor for Latentspeed Trading Engine
Monitors preprocessed trade and orderbook data from trading_engine_service
"""

import zmq
import json
import time
import threading
import signal
import sys
from collections import defaultdict
from datetime import datetime

class MarketDataMonitor:
    def __init__(self, host="127.0.0.1", trades_port=5556, books_port=5557):
        self.host = host
        self.trades_port = trades_port
        self.books_port = books_port
        
        # Statistics
        self.stats = {
            'trades_received': 0,
            'books_received': 0,
            'start_time': time.time(),
            'last_trade': {},
            'last_book': {},
            'symbols_seen': set(),
            'exchanges_seen': set()
        }
        
        # ZMQ setup
        self.context = zmq.Context()
        self.trades_socket = None
        self.books_socket = None
        self.running = False
        
        # Setup signal handler for graceful shutdown
        signal.signal(signal.SIGINT, self.signal_handler)
        
    def signal_handler(self, signum, frame):
        """Handle Ctrl+C gracefully"""
        print(f"\n[INFO] Received signal {signum}, shutting down...")
        self.running = False
        
    def connect(self):
        """Connect to ZMQ endpoints"""
        try:
            # Trades subscriber
            self.trades_socket = self.context.socket(zmq.SUB)
            self.trades_socket.connect(f"tcp://{self.host}:{self.trades_port}")
            self.trades_socket.setsockopt(zmq.SUBSCRIBE, b"")  # Subscribe to all topics
            self.trades_socket.setsockopt(zmq.RCVHWM, 10000)
            
            # Books subscriber  
            self.books_socket = self.context.socket(zmq.SUB)
            self.books_socket.connect(f"tcp://{self.host}:{self.books_port}")
            self.books_socket.setsockopt(zmq.SUBSCRIBE, b"")  # Subscribe to all topics
            self.books_socket.setsockopt(zmq.RCVHWM, 10000)
            
            print(f"[CONN] Connected to trades: tcp://{self.host}:{self.trades_port}")
            print(f"[CONN] Connected to books: tcp://{self.host}:{self.books_port}")
            return True
            
        except Exception as e:
            print(f"[ERROR] Failed to connect: {e}")
            return False
            
    def disconnect(self):
        """Clean up ZMQ connections"""
        if self.trades_socket:
            self.trades_socket.close()
        if self.books_socket:
            self.books_socket.close()
        self.context.term()
        
    def handle_trade_message(self, topic, payload):
        """Process trade data from trading_engine_service"""
        try:
            trade_data = json.loads(payload)
            self.stats['trades_received'] += 1
            
            # DEBUG: Show raw JSON for first few messages
            if self.stats['trades_received'] <= 3:
                print(f"[DEBUG] Raw JSON payload #{self.stats['trades_received']}:")
                print(f"  {payload}")
                print()
            
            # Extract key fields matching C++ TradeData structure
            exchange = trade_data.get('exchange', 'UNKNOWN')
            symbol = trade_data.get('symbol', 'UNKNOWN') 
            price = trade_data.get('price', 0)
            amount = trade_data.get('amount', 0)
            side = trade_data.get('side', 'UNKNOWN')
            timestamp_ns = trade_data.get('timestamp_ns', 0)
            trade_id = trade_data.get('trade_id', 'N/A')
            
            # Derived fields from preprocessing
            volatility = trade_data.get('volatility_transaction_price', 0)
            window_size = trade_data.get('transaction_price_window_size', 0)
            seq = trade_data.get('sequence_number', 0)
            
            # Calculate notional
            notional = price * amount if price and amount else 0
            
            # Update stats
            self.stats['symbols_seen'].add(symbol)
            self.stats['exchanges_seen'].add(exchange)
            self.stats['last_trade'] = trade_data
            
            # Format timestamp
            ts_str = datetime.fromtimestamp(timestamp_ns / 1e9).strftime('%H:%M:%S.%f')[:-3] if timestamp_ns else 'N/A'
            
            print(f"[TRADE #{self.stats['trades_received']:>5}] {topic}")
            print(f"  Exchange: {exchange:<8} | Symbol: {symbol:<12} | Side: {side.upper():<4}")
            print(f"  Price: ${price:>12,.2f} | Amount: {amount:>12,.6f} | Notional: ${notional:>12,.2f}")
            print(f"  Volatility: {volatility:>8.6f} | Window: {window_size:>3} | Seq: {seq:>6} | ID: {trade_id}")
            print(f"  Time: {ts_str}")
            print()
            
        except json.JSONDecodeError as e:
            print(f"[ERROR] Failed to parse trade JSON: {e}")
            print(f"[ERROR] Raw payload: {payload}")
        except Exception as e:
            print(f"[ERROR] Error processing trade: {e}")
            print(f"[ERROR] Raw payload: {payload}")
            
    def handle_book_message(self, topic, payload):
        """Process orderbook data from trading_engine_service"""
        try:
            book_data = json.loads(payload)
            self.stats['books_received'] += 1
            
            # Extract key fields matching C++ OrderBookData structure
            exchange = book_data.get('exchange', 'UNKNOWN')
            symbol = book_data.get('symbol', 'UNKNOWN')
            timestamp_ns = book_data.get('timestamp_ns', 0)
            
            # Best bid/ask
            best_bid_price = book_data.get('best_bid_price', 0)
            best_bid_size = book_data.get('best_bid_size', 0)
            best_ask_price = book_data.get('best_ask_price', 0)
            best_ask_size = book_data.get('best_ask_size', 0)
            
            # Derived metrics from preprocessing
            midpoint = book_data.get('midpoint', 0)
            relative_spread = book_data.get('relative_spread', 0)
            imbalance_lvl1 = book_data.get('imbalance_lvl1', 0)
            volatility_mid = book_data.get('volatility_mid', 0)
            ofi_rolling = book_data.get('ofi_rolling', 0)
            seq = book_data.get('sequence_number', 0)
            
            # Update stats
            self.stats['symbols_seen'].add(symbol)
            self.stats['exchanges_seen'].add(exchange)
            self.stats['last_book'] = book_data
            
            # Calculate spread in bps
            spread_bps = relative_spread * 10000 if relative_spread else 0
            
            # Format timestamp
            ts_str = datetime.fromtimestamp(timestamp_ns / 1e9).strftime('%H:%M:%S.%f')[:-3] if timestamp_ns else 'N/A'
            
            print(f"[BOOK  #{self.stats['books_received']:>5}] {topic}")
            print(f"  Exchange: {exchange:<8} | Symbol: {symbol:<12}")
            print(f"  Bid: ${best_bid_price:>12,.2f} ({best_bid_size:>8,.4f}) | Ask: ${best_ask_price:>12,.2f} ({best_ask_size:>8,.4f})")
            print(f"  Mid: ${midpoint:>12,.2f} | Spread: {spread_bps:>8.2f}bps | Imbalance: {imbalance_lvl1:>8.4f}")
            print(f"  Vol_Mid: {volatility_mid:>8.6f} | OFI: {ofi_rolling:>8.6f} | Seq: {seq:>6}")
            print(f"  Time: {ts_str}")
            print()
            
        except json.JSONDecodeError as e:
            print(f"[ERROR] Failed to parse book JSON: {e}")
        except Exception as e:
            print(f"[ERROR] Error processing book: {e}")
            
    def listen_trades(self):
        """Listen for trade messages"""
        while self.running:
            try:
                # Receive multipart message: [topic, payload]
                message = self.trades_socket.recv_multipart(zmq.NOBLOCK)
                if len(message) >= 2:
                    topic = message[0].decode('utf-8')
                    payload = message[1].decode('utf-8')
                    self.handle_trade_message(topic, payload)
                    
            except zmq.Again:
                time.sleep(0.001)  # No message available
            except Exception as e:
                if self.running:  # Only log if we're supposed to be running
                    print(f"[ERROR] Trade listener error: {e}")
                break
                
    def listen_books(self):
        """Listen for orderbook messages"""
        while self.running:
            try:
                # Receive multipart message: [topic, payload]
                message = self.books_socket.recv_multipart(zmq.NOBLOCK)
                if len(message) >= 2:
                    topic = message[0].decode('utf-8')
                    payload = message[1].decode('utf-8')
                    self.handle_book_message(topic, payload)
                    
            except zmq.Again:
                time.sleep(0.001)  # No message available
            except Exception as e:
                if self.running:  # Only log if we're supposed to be running
                    print(f"[ERROR] Book listener error: {e}")
                break
                
    def print_summary(self):
        """Print periodic summary"""
        while self.running:
            time.sleep(10)  # Print every 10 seconds
            
            elapsed = time.time() - self.stats['start_time']
            trade_rate = self.stats['trades_received'] / elapsed if elapsed > 0 else 0
            book_rate = self.stats['books_received'] / elapsed if elapsed > 0 else 0
            
            print("\n" + "="*80)
            print(f"MARKET DATA SUMMARY (Elapsed: {elapsed:.1f}s)")
            print("="*80)
            print(f"Trades Received: {self.stats['trades_received']:>8} (Rate: {trade_rate:>6.2f}/sec)")
            print(f"Books Received:  {self.stats['books_received']:>8} (Rate: {book_rate:>6.2f}/sec)")
            print(f"Symbols Seen:    {len(self.stats['symbols_seen']):>8} {list(self.stats['symbols_seen'])}")
            print(f"Exchanges Seen:  {len(self.stats['exchanges_seen']):>8} {list(self.stats['exchanges_seen'])}")
            print("="*80 + "\n")
            
    def run(self):
        """Main run loop"""
        if not self.connect():
            return
            
        self.running = True
        
        print("\n" + "="*80)
        print("LATENTSPEED MARKET DATA MONITOR")
        print("="*80)
        print(f"Listening to trading_engine_service on {self.host}")
        print(f"Trades endpoint: tcp://{self.host}:{self.trades_port}")
        print(f"Books endpoint: tcp://{self.host}:{self.books_port}")
        print("Waiting for market data... (Press Ctrl+C to stop)")
        print("="*80 + "\n")
        
        # Start listener threads
        trade_thread = threading.Thread(target=self.listen_trades, daemon=True)
        book_thread = threading.Thread(target=self.listen_books, daemon=True)
        summary_thread = threading.Thread(target=self.print_summary, daemon=True)
        
        trade_thread.start()
        book_thread.start()
        summary_thread.start()
        
        try:
            # Keep main thread alive
            while self.running:
                time.sleep(0.1)
        except KeyboardInterrupt:
            pass
        finally:
            self.running = False
            self.disconnect()
            
            # Final summary
            elapsed = time.time() - self.stats['start_time']
            print(f"\n[FINAL] Session lasted {elapsed:.1f}s")
            print(f"[FINAL] Total trades: {self.stats['trades_received']}")
            print(f"[FINAL] Total books: {self.stats['books_received']}")
            print("[FINAL] Monitor stopped.")

def main():
    """Main entry point"""
    # Parse command line arguments
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    trades_port = int(sys.argv[2]) if len(sys.argv) > 2 else 5556
    books_port = int(sys.argv[3]) if len(sys.argv) > 3 else 5557
    
    print("Latentspeed Trading Engine - Market Data Monitor")
    print(f"Host: {host}")
    print(f"Trades Port: {trades_port}")
    print(f"Books Port: {books_port}")
    print("Usage: python market_data_monitor.py [host] [trades_port] [books_port]")
    print()
    
    # Create and run monitor
    monitor = MarketDataMonitor(host, trades_port, books_port)
    monitor.run()

if __name__ == "__main__":
    main()
