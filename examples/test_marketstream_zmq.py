#!/usr/bin/env python3
"""
Test script to consume market data from marketstream ZMQ publishers
Connects to both trades (5556) and orderbooks (5557) streams

Usage:
    python test_marketstream_zmq.py
    python test_marketstream_zmq.py --trades-only
    python test_marketstream_zmq.py --books-only
"""

import zmq
import json
import time
import argparse
from datetime import datetime
from collections import defaultdict


class MarketStreamConsumer:
    """Consumer for marketstream ZMQ data"""
    
    def __init__(self, trades_port=5556, books_port=5557, enable_trades=True, enable_books=True):
        self.context = zmq.Context()
        self.trades_socket = None
        self.books_socket = None
        
        # Statistics
        self.stats = defaultdict(lambda: {"trades": 0, "books": 0})
        self.start_time = time.time()
        
        # Connect to trades stream
        if enable_trades:
            self.trades_socket = self.context.socket(zmq.SUB)
            self.trades_socket.connect(f"tcp://127.0.0.1:{trades_port}")
            self.trades_socket.setsockopt_string(zmq.SUBSCRIBE, "")
            self.trades_socket.setsockopt(zmq.RCVTIMEO, 100)  # 100ms timeout
            print(f"✓ Connected to trades stream: tcp://127.0.0.1:{trades_port}")
        
        # Connect to orderbook stream
        if enable_books:
            self.books_socket = self.context.socket(zmq.SUB)
            self.books_socket.connect(f"tcp://127.0.0.1:{books_port}")
            self.books_socket.setsockopt_string(zmq.SUBSCRIBE, "")
            self.books_socket.setsockopt(zmq.RCVTIMEO, 100)  # 100ms timeout
            print(f"✓ Connected to orderbook stream: tcp://127.0.0.1:{books_port}")
    
    def format_trade(self, trade):
        """Format trade data for display"""
        exchange = trade.get("exchange", "N/A")
        symbol = trade.get("symbol", "N/A")
        price = trade.get("price", 0)
        amount = trade.get("amount", 0)
        side = trade.get("side", "N/A")
        seq = trade.get("seq", 0)
        
        # Color code by side
        side_color = "\033[92m" if side == "buy" else "\033[91m"  # Green for buy, red for sell
        reset_color = "\033[0m"
        
        return f"[TRADE #{seq}] {exchange}:{symbol} | {side_color}{side.upper():4}{reset_color} | Price: ${price:>10,.2f} | Amount: {amount:>10.4f}"
    
    def format_orderbook(self, book):
        """Format orderbook data for display"""
        exchange = book.get("exchange", "N/A")
        symbol = book.get("symbol", "N/A")
        seq = book.get("seq", 0)
        
        bids = book.get("bids", [])
        asks = book.get("asks", [])
        
        # Calculate mid price and spread
        best_bid = bids[0]["price"] if bids else 0
        best_ask = asks[0]["price"] if asks else 0
        mid_price = (best_bid + best_ask) / 2 if best_bid and best_ask else 0
        spread = best_ask - best_bid if best_bid and best_ask else 0
        spread_bps = (spread / mid_price * 10000) if mid_price > 0 else 0
        
        output = [
            f"\n{'='*70}",
            f"[ORDERBOOK #{seq}] {exchange}:{symbol}",
            f"Mid: ${mid_price:,.2f} | Spread: ${spread:.2f} ({spread_bps:.2f} bps)",
            f"{'='*70}",
            f"{'BIDS':<35} | {'ASKS':<35}",
            f"{'-'*70}"
        ]
        
        # Show top 5 levels
        max_levels = min(5, max(len(bids), len(asks)))
        for i in range(max_levels):
            bid_str = ""
            ask_str = ""
            
            if i < len(bids):
                bid = bids[i]
                bid_str = f"${bid['price']:>10,.2f} x {bid['quantity']:>8.4f}"
            
            if i < len(asks):
                ask = asks[i]
                ask_str = f"${ask['price']:>10,.2f} x {ask['quantity']:>8.4f}"
            
            output.append(f"{bid_str:<35} | {ask_str:<35}")
        
        return "\n".join(output)
    
    def display_stats(self):
        """Display statistics"""
        elapsed = time.time() - self.start_time
        
        print(f"\n{'='*70}")
        print(f"Statistics (Running for {elapsed:.1f}s)")
        print(f"{'='*70}")
        print(f"{'Exchange:Symbol':<25} {'Trades':<15} {'Books':<15} {'Rate (msg/s)'}")
        print(f"{'-'*70}")
        
        total_trades = 0
        total_books = 0
        
        for key, counts in sorted(self.stats.items()):
            trades = counts["trades"]
            books = counts["books"]
            total_msgs = trades + books
            rate = total_msgs / elapsed if elapsed > 0 else 0
            
            total_trades += trades
            total_books += books
            
            print(f"{key:<25} {trades:<15} {books:<15} {rate:>8.2f}")
        
        print(f"{'-'*70}")
        total_msgs = total_trades + total_books
        total_rate = total_msgs / elapsed if elapsed > 0 else 0
        print(f"{'TOTAL':<25} {total_trades:<15} {total_books:<15} {total_rate:>8.2f}")
        print(f"{'='*70}\n")
    
    def run(self, show_trades=True, show_books=True, stats_interval=10):
        """Main loop to consume and display data"""
        print(f"\n{'='*70}")
        print(f"MarketStream ZMQ Consumer - Listening for data...")
        print(f"Trades: {'ENABLED' if self.trades_socket else 'DISABLED'}")
        print(f"Books: {'ENABLED' if self.books_socket else 'DISABLED'}")
        print(f"Press Ctrl+C to stop")
        print(f"{'='*70}\n")
        
        last_stats_time = time.time()
        
        try:
            while True:
                # Receive trades
                if self.trades_socket:
                    try:
                        trade_data = self.trades_socket.recv_json(flags=zmq.NOBLOCK)
                        
                        # Update stats
                        key = f"{trade_data.get('exchange', 'N/A')}:{trade_data.get('symbol', 'N/A')}"
                        self.stats[key]["trades"] += 1
                        
                        # Display trade
                        if show_trades:
                            print(self.format_trade(trade_data))
                    
                    except zmq.Again:
                        pass  # No message available
                    except json.JSONDecodeError as e:
                        print(f"⚠ Failed to decode trade JSON: {e}")
                    except Exception as e:
                        print(f"⚠ Error receiving trade: {e}")
                
                # Receive orderbooks
                if self.books_socket:
                    try:
                        book_data = self.books_socket.recv_json(flags=zmq.NOBLOCK)
                        
                        # Update stats
                        key = f"{book_data.get('exchange', 'N/A')}:{book_data.get('symbol', 'N/A')}"
                        self.stats[key]["books"] += 1
                        
                        # Display orderbook
                        if show_books:
                            print(self.format_orderbook(book_data))
                    
                    except zmq.Again:
                        pass  # No message available
                    except json.JSONDecodeError as e:
                        print(f"⚠ Failed to decode book JSON: {e}")
                    except Exception as e:
                        print(f"⚠ Error receiving book: {e}")
                
                # Display stats periodically
                if time.time() - last_stats_time >= stats_interval:
                    self.display_stats()
                    last_stats_time = time.time()
                
                # Small sleep to prevent CPU spinning
                time.sleep(0.001)
        
        except KeyboardInterrupt:
            print("\n\n⚠ Interrupted by user")
        
        finally:
            # Final stats
            print("\nFinal Statistics:")
            self.display_stats()
            
            # Cleanup
            if self.trades_socket:
                self.trades_socket.close()
            if self.books_socket:
                self.books_socket.close()
            self.context.term()
            print("✓ Disconnected")


def main():
    parser = argparse.ArgumentParser(
        description="Test script for marketstream ZMQ data consumption",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Default: show both trades and books
  python test_marketstream_zmq.py
  
  # Show trades only
  python test_marketstream_zmq.py --trades-only
  
  # Show books only
  python test_marketstream_zmq.py --books-only
  
  # Custom ports
  python test_marketstream_zmq.py --trades-port 5556 --books-port 5557
  
  # Hide output, just show stats
  python test_marketstream_zmq.py --quiet
        """
    )
    
    parser.add_argument("--trades-port", type=int, default=5556,
                        help="ZMQ port for trades stream (default: 5556)")
    parser.add_argument("--books-port", type=int, default=5557,
                        help="ZMQ port for orderbooks stream (default: 5557)")
    parser.add_argument("--trades-only", action="store_true",
                        help="Only consume trades, not orderbooks")
    parser.add_argument("--books-only", action="store_true",
                        help="Only consume orderbooks, not trades")
    parser.add_argument("--quiet", action="store_true",
                        help="Don't show individual messages, only stats")
    parser.add_argument("--stats-interval", type=int, default=10,
                        help="Interval in seconds to display stats (default: 10)")
    
    args = parser.parse_args()
    
    # Determine what to enable
    enable_trades = not args.books_only
    enable_books = not args.trades_only
    
    # Determine what to show
    show_trades = enable_trades and not args.quiet
    show_books = enable_books and not args.quiet
    
    # Create consumer
    consumer = MarketStreamConsumer(
        trades_port=args.trades_port,
        books_port=args.books_port,
        enable_trades=enable_trades,
        enable_books=enable_books
    )
    
    # Run
    consumer.run(
        show_trades=show_trades,
        show_books=show_books,
        stats_interval=args.stats_interval
    )


if __name__ == "__main__":
    main()
