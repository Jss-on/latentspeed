#!/usr/bin/env python3
"""
dYdX Streaming Verification Tool
Tests and monitors dYdX market data streaming through ZMQ

Usage:
    python verify_dydx_streaming.py                    # Monitor all dYdX streams
    python verify_dydx_streaming.py --trades-only       # Monitor trades only
    python verify_dydx_streaming.py --duration 30       # Run for 30 seconds
"""

import zmq
import json
import time
import argparse
import sys
from datetime import datetime
from collections import defaultdict
from typing import Dict, List

class DydxStreamMonitor:
    def __init__(self, trades_port: int = 5556, books_port: int = 5557):
        self.trades_port = trades_port
        self.books_port = books_port
        self.context = zmq.Context()
        
        # Statistics
        self.stats = {
            'trades': defaultdict(int),
            'orderbooks': defaultdict(int),
            'errors': 0,
            'start_time': time.time()
        }
        
        # Track data quality
        self.price_ranges = defaultdict(lambda: {'min': float('inf'), 'max': 0})
        self.latest_data = {}
        
    def connect(self, monitor_trades: bool = True, monitor_books: bool = True):
        """Connect to ZMQ sockets"""
        self.sockets = {}
        
        if monitor_trades:
            print(f"📊 Connecting to trades stream (port {self.trades_port})...")
            trades_sock = self.context.socket(zmq.SUB)
            trades_sock.connect(f"tcp://localhost:{self.trades_port}")
            trades_sock.setsockopt_string(zmq.SUBSCRIBE, "DYDX-")
            trades_sock.setsockopt(zmq.RCVTIMEO, 1000)  # 1 second timeout
            self.sockets['trades'] = trades_sock
            
        if monitor_books:
            print(f"📈 Connecting to orderbook stream (port {self.books_port})...")
            books_sock = self.context.socket(zmq.SUB)
            books_sock.connect(f"tcp://localhost:{self.books_port}")
            books_sock.setsockopt_string(zmq.SUBSCRIBE, "DYDX-")
            books_sock.setsockopt(zmq.RCVTIMEO, 1000)
            self.sockets['orderbooks'] = books_sock
    
    def process_trade(self, topic: str, data: dict):
        """Process and validate trade message"""
        try:
            symbol = data.get('symbol', 'UNKNOWN')
            self.stats['trades'][symbol] += 1
            
            # Update price range
            price = float(data.get('price', 0))
            if price > 0:
                self.price_ranges[symbol]['min'] = min(self.price_ranges[symbol]['min'], price)
                self.price_ranges[symbol]['max'] = max(self.price_ranges[symbol]['max'], price)
            
            # Store latest
            self.latest_data[f"trade_{symbol}"] = data
            
            # Print
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            print(f"\n[{timestamp}] TRADE - {symbol}")
            print(f"  Price: {price:,.2f}  Amount: {data.get('amount', 0):.8f}  Side: {data.get('side', 'N/A')}")
            
            # Show preprocessed metrics if available
            if 'transaction_price' in data:
                print(f"  Txn Price: {data['transaction_price']:,.2f}")
            if 'volatility_transaction_price' in data:
                print(f"  Volatility: {data['volatility_transaction_price']:.6f}")
            if 'trading_volume' in data:
                print(f"  Volume: {data['trading_volume']:,.2f}")
                
        except Exception as e:
            print(f"❌ Error processing trade: {e}")
            self.stats['errors'] += 1
    
    def process_orderbook(self, topic: str, data: dict):
        """Process and validate orderbook message"""
        try:
            symbol = data.get('symbol', 'UNKNOWN')
            self.stats['orderbooks'][symbol] += 1
            
            # Store latest
            self.latest_data[f"book_{symbol}"] = data
            
            # Extract data
            bids = data.get('bids', [])
            asks = data.get('asks', [])
            
            if not bids or not asks:
                print(f"⚠️  Empty orderbook for {symbol}")
                return
            
            best_bid = bids[0]
            best_ask = asks[0]
            
            # Print
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            print(f"\n[{timestamp}] ORDERBOOK - {symbol}")
            print(f"  Bid: {best_bid.get('price', 0):,.2f} @ {best_bid.get('quantity', 0):.8f}")
            print(f"  Ask: {best_ask.get('price', 0):,.2f} @ {best_ask.get('quantity', 0):.8f}")
            
            # Show preprocessed metrics
            if 'midpoint' in data:
                print(f"  Mid: {data['midpoint']:,.2f}", end='')
            if 'relative_spread' in data:
                spread_bps = data['relative_spread'] * 10000
                print(f"  Spread: {spread_bps:.2f} bps", end='')
            if 'imbalance_lvl1' in data:
                print(f"  Imbalance: {data['imbalance_lvl1']:.4f}", end='')
            print()
            
            if 'ofi_rolling' in data:
                print(f"  OFI: {data['ofi_rolling']:.4f}", end='')
            if 'volatility_mid' in data:
                print(f"  Vol: {data['volatility_mid']:.6f}", end='')
            print()
            
        except Exception as e:
            print(f"❌ Error processing orderbook: {e}")
            self.stats['errors'] += 1
    
    def monitor(self, duration: int = 0):
        """Monitor streams for specified duration (0 = infinite)"""
        print("\n" + "=" * 80)
        print("📡 dYdX STREAMING MONITOR")
        print("=" * 80)
        print("Press Ctrl+C to stop\n")
        
        start_time = time.time()
        last_stats_time = start_time
        
        try:
            while True:
                # Check duration
                if duration > 0 and (time.time() - start_time) >= duration:
                    break
                
                # Poll trades
                if 'trades' in self.sockets:
                    try:
                        topic = self.sockets['trades'].recv_string(zmq.NOBLOCK)
                        message = self.sockets['trades'].recv_string(zmq.NOBLOCK)
                        data = json.loads(message)
                        self.process_trade(topic, data)
                    except zmq.Again:
                        pass
                    except Exception as e:
                        print(f"❌ Trade error: {e}")
                
                # Poll orderbooks
                if 'orderbooks' in self.sockets:
                    try:
                        topic = self.sockets['orderbooks'].recv_string(zmq.NOBLOCK)
                        message = self.sockets['orderbooks'].recv_string(zmq.NOBLOCK)
                        data = json.loads(message)
                        self.process_orderbook(topic, data)
                    except zmq.Again:
                        pass
                    except Exception as e:
                        print(f"❌ Orderbook error: {e}")
                
                # Print stats every 10 seconds
                if time.time() - last_stats_time >= 10:
                    self.print_stats()
                    last_stats_time = time.time()
                
                time.sleep(0.001)
                
        except KeyboardInterrupt:
            print("\n\n⏹️  Stopping monitor...")
    
    def print_stats(self):
        """Print statistics summary"""
        elapsed = time.time() - self.stats['start_time']
        
        print("\n" + "=" * 80)
        print(f"📊 STATISTICS ({elapsed:.1f}s)")
        print("=" * 80)
        
        # Trade stats
        total_trades = sum(self.stats['trades'].values())
        if total_trades > 0:
            print(f"\n📈 TRADES: {total_trades} total ({total_trades/elapsed:.2f}/sec)")
            for symbol, count in sorted(self.stats['trades'].items()):
                print(f"  {symbol}: {count} ({count/elapsed:.2f}/sec)")
        
        # Orderbook stats
        total_books = sum(self.stats['orderbooks'].values())
        if total_books > 0:
            print(f"\n📊 ORDERBOOKS: {total_books} total ({total_books/elapsed:.2f}/sec)")
            for symbol, count in sorted(self.stats['orderbooks'].items()):
                print(f"  {symbol}: {count} ({count/elapsed:.2f}/sec)")
        
        # Price ranges
        if self.price_ranges:
            print(f"\n💰 PRICE RANGES:")
            for symbol, range_data in sorted(self.price_ranges.items()):
                if range_data['max'] > 0:
                    print(f"  {symbol}: ${range_data['min']:,.2f} - ${range_data['max']:,.2f}")
        
        # Errors
        if self.stats['errors'] > 0:
            print(f"\n❌ ERRORS: {self.stats['errors']}")
        
        print("=" * 80)
    
    def health_check(self) -> bool:
        """Perform health check on the streams"""
        print("\n🏥 Running health check...")
        
        timeout = 10
        start = time.time()
        received_trade = False
        received_book = False
        
        while (time.time() - start) < timeout:
            if 'trades' in self.sockets and not received_trade:
                try:
                    topic = self.sockets['trades'].recv_string(zmq.NOBLOCK)
                    message = self.sockets['trades'].recv_string(zmq.NOBLOCK)
                    data = json.loads(message)
                    if data.get('exchange') == 'DYDX':
                        print("✅ Trades stream: HEALTHY")
                        received_trade = True
                except zmq.Again:
                    pass
            
            if 'orderbooks' in self.sockets and not received_book:
                try:
                    topic = self.sockets['orderbooks'].recv_string(zmq.NOBLOCK)
                    message = self.sockets['orderbooks'].recv_string(zmq.NOBLOCK)
                    data = json.loads(message)
                    if data.get('exchange') == 'DYDX':
                        print("✅ Orderbook stream: HEALTHY")
                        received_book = True
                except zmq.Again:
                    pass
            
            if received_trade and received_book:
                return True
            
            time.sleep(0.1)
        
        if not received_trade:
            print("❌ Trades stream: NO DATA")
        if not received_book:
            print("❌ Orderbook stream: NO DATA")
        
        return False
    
    def cleanup(self):
        """Close connections"""
        for sock in self.sockets.values():
            sock.close()
        self.context.term()

def main():
    parser = argparse.ArgumentParser(
        description='dYdX Streaming Verification Tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                          # Monitor all streams
  %(prog)s --trades-only            # Monitor trades only
  %(prog)s --orderbook-only         # Monitor orderbooks only
  %(prog)s --duration 60            # Run for 60 seconds
  %(prog)s --health-check           # Check stream health
        """
    )
    
    parser.add_argument('--trades-port', type=int, default=5556,
                        help='Trades ZMQ port (default: 5556)')
    parser.add_argument('--orderbook-port', type=int, default=5557,
                        help='Orderbook ZMQ port (default: 5557)')
    parser.add_argument('--trades-only', action='store_true',
                        help='Monitor trades only')
    parser.add_argument('--orderbook-only', action='store_true',
                        help='Monitor orderbooks only')
    parser.add_argument('--duration', type=int, default=0,
                        help='Monitor duration in seconds (0 = infinite)')
    parser.add_argument('--health-check', action='store_true',
                        help='Perform health check and exit')
    
    args = parser.parse_args()
    
    # Create monitor
    monitor = DydxStreamMonitor(
        trades_port=args.trades_port,
        books_port=args.orderbook_port
    )
    
    # Connect
    monitor_trades = not args.orderbook_only
    monitor_books = not args.trades_only
    
    try:
        monitor.connect(monitor_trades=monitor_trades, monitor_books=monitor_books)
        
        if args.health_check:
            # Health check mode
            healthy = monitor.health_check()
            monitor.cleanup()
            sys.exit(0 if healthy else 1)
        else:
            # Normal monitoring mode
            monitor.monitor(duration=args.duration)
            
    except Exception as e:
        print(f"\n❌ Fatal error: {e}")
        import traceback
        traceback.print_exc()
        return 1
    finally:
        monitor.print_stats()
        monitor.cleanup()
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
