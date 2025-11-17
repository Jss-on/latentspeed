#!/usr/bin/env python3
"""
Simple Momentum Strategy for Hyperliquid
Connects to:
  - Marketstream (ZMQ) for market data
  - Trading Engine (ZMQ) for order execution

Strategy Logic:
  - Tracks short-term momentum using trade data
  - Buys on positive momentum, sells on negative momentum
  - Simple position management with max position size
"""

import zmq
import json
import time
import logging
from collections import deque, defaultdict
from datetime import datetime
import signal
import sys

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)
logger = logging.getLogger(__name__)

class MomentumStrategy:
    def __init__(
        self,
        symbol="BTC",
        position_size=0.001,  # Base position size
        max_position=0.01,    # Max total position
        momentum_window=20,   # Number of trades for momentum calc
        momentum_threshold=0.0005,  # 0.05% price move threshold
    ):
        self.symbol = symbol
        self.position_size = position_size
        self.max_position = max_position
        self.momentum_window = momentum_window
        self.momentum_threshold = momentum_threshold
        
        # Track trades for momentum
        self.trade_prices = deque(maxlen=momentum_window)
        
        # Track position
        self.current_position = 0.0
        self.last_order_time = 0
        self.order_cooldown = 5  # seconds between orders
        
        # Stats
        self.orders_sent = 0
        self.last_price = 0.0
        
        # ZMQ setup
        self.context = zmq.Context()
        
        # Subscribe to marketstream trades
        self.market_sub = self.context.socket(zmq.SUB)
        self.market_sub.connect("tcp://127.0.0.1:5556")
        self.market_sub.setsockopt_string(zmq.SUBSCRIBE, "")
        
        # Connect to trading engine (PUSH to match engine's PULL socket)
        self.order_client = self.context.socket(zmq.PUSH)
        self.order_client.connect("tcp://127.0.0.1:5601")
        self.order_client.setsockopt(zmq.SNDHWM, 1000)  # High water mark
        self.order_client.setsockopt(zmq.SNDTIMEO, 5000)  # 5 sec send timeout
        
        # Subscribe to order reports
        self.report_sub = self.context.socket(zmq.SUB)
        self.report_sub.connect("tcp://127.0.0.1:5602")
        self.report_sub.setsockopt_string(zmq.SUBSCRIBE, "")
        self.report_sub.setsockopt(zmq.RCVTIMEO, 100)  # Non-blocking
        
        logger.info(f"Strategy initialized for {symbol}")
        logger.info(f"Position size: {position_size}, Max: {max_position}")
        logger.info(f"Momentum window: {momentum_window}, Threshold: {momentum_threshold}")
    
    def calculate_momentum(self):
        """Calculate momentum from recent trades"""
        if len(self.trade_prices) < 2:
            return 0.0
        
        first_price = self.trade_prices[0]
        last_price = self.trade_prices[-1]
        
        momentum = (last_price - first_price) / first_price
        return momentum
    
    def should_trade(self):
        """Check if enough time has passed since last order"""
        current_time = time.time()
        if current_time - self.last_order_time < self.order_cooldown:
            return False
        return True
    
    def reconnect_order_client(self):
        """Reconnect order client socket (needed after error)"""
        try:
            self.order_client.close()
        except:
            pass
        
        self.order_client = self.context.socket(zmq.PUSH)
        self.order_client.connect("tcp://127.0.0.1:5601")
        self.order_client.setsockopt(zmq.SNDHWM, 1000)
        self.order_client.setsockopt(zmq.SNDTIMEO, 5000)
        logger.info("Order socket reconnected")
    
    def send_order(self, side, quantity, price):
        """Send order to trading engine (fire-and-forget via PUSH socket)"""
        if not self.should_trade():
            return False
        
        cl_id = f"momentum_{int(time.time() * 1000)}"
        
        order = {
            "version": 1,
            "cl_id": cl_id,
            "action": "place",
            "venue_type": "cex",
            "venue": "hyperliquid",
            "product_type": "perpetual",
            "details": {
                "symbol": self.symbol,
                "side": side.lower(),
                "order_type": "limit",
                "price": str(price),
                "size": str(quantity),
                "reduce_only": "false"
            },
            "ts_ns": int(time.time() * 1_000_000_000),
            "tags": {
                "strategy": "momentum"
            }
        }
        
        try:
            # PUSH socket - fire and forget, no response expected
            self.order_client.send_string(json.dumps(order))
            
            self.orders_sent += 1
            self.last_order_time = time.time()
            
            logger.info(f"Order sent: {side.upper()} {quantity} {self.symbol} @ ${price:.2f}")
            logger.debug(f"Order ID: {cl_id} - Waiting for confirmation...")
            return True
            
        except zmq.error.Again:
            logger.warning("Order send queue full - order dropped")
            return False
        except Exception as e:
            logger.error(f"Order send error: {e} - reconnecting socket")
            self.reconnect_order_client()
            return False
    
    def process_trade(self, trade_data):
        """Process incoming trade and make trading decision"""
        try:
            # Parse trade
            exchange = trade_data.get("exchange", "")
            symbol = trade_data.get("symbol", "")
            price = float(trade_data.get("price", 0))
            amount = float(trade_data.get("amount", 0))
            side = trade_data.get("side", "")
            
            # Filter for our symbol
            if symbol != self.symbol:
                return
            
            self.last_price = price
            self.trade_prices.append(price)
            
            # Need enough data
            if len(self.trade_prices) < self.momentum_window:
                return
            
            # Calculate momentum
            momentum = self.calculate_momentum()
            
            # Trading logic
            if momentum > self.momentum_threshold:
                # Positive momentum - BUY
                if self.current_position < self.max_position:
                    logger.info(f"📈 BUY signal - Momentum: {momentum:.4f}")
                    if self.send_order("buy", self.position_size, price * 0.999):  # Slightly below market
                        self.current_position += self.position_size
                        
            elif momentum < -self.momentum_threshold:
                # Negative momentum - SELL
                if self.current_position > -self.max_position:
                    logger.info(f"📉 SELL signal - Momentum: {momentum:.4f}")
                    if self.send_order("sell", self.position_size, price * 1.001):  # Slightly above market
                        self.current_position -= self.position_size
            
        except Exception as e:
            logger.error(f"Error processing trade: {e}")
    
    def process_report(self, report_data):
        """Process order execution reports"""
        try:
            # Log complete raw report data first
            logger.debug(f"Raw report data: {json.dumps(report_data, indent=2)}")
            
            # Handle new engine format (status-based)
            status = report_data.get("status", "")
            cl_id = report_data.get("cl_id", "")
            reason_code = report_data.get("reason_code", "")
            reason_text = report_data.get("reason_text", "")
            
            # Extract details if present
            details = report_data.get("details", {})
            symbol = details.get("symbol", "")
            side = details.get("side", "")
            price = details.get("price", "")
            size = details.get("size", "")
            
            # Handle different status types
            if status == "accepted":
                logger.info(f"✅ ORDER ACCEPTED - {side.upper()} {size} {symbol} @ ${price}")
                logger.info(f"   Client ID: {cl_id}")
                logger.debug(f"   Full data: {report_data}")
                
            elif status == "filled":
                filled_qty = details.get("filled_size", size)
                avg_price = details.get("avg_price", price)
                logger.info(f"💰 ORDER FILLED - {side.upper()} {filled_qty} {symbol} @ ${avg_price}")
                logger.info(f"   Client ID: {cl_id}")
                logger.debug(f"   Full data: {report_data}")
                
            elif status == "partial":
                filled_qty = details.get("filled_size", 0)
                remaining = details.get("remaining_size", 0)
                avg_price = details.get("avg_price", price)
                logger.info(f"📊 PARTIAL FILL - Filled: {filled_qty}, Remaining: {remaining} @ ${avg_price}")
                logger.info(f"   Client ID: {cl_id}")
                logger.debug(f"   Full data: {report_data}")
                
            elif status == "cancelled":
                logger.warning(f"❌ ORDER CANCELLED - {cl_id}")
                logger.debug(f"   Full data: {report_data}")
                
            elif status == "rejected":
                logger.warning(f"⚠️  ORDER REJECTED - {cl_id}")
                logger.warning(f"   Reason: {reason_code} - {reason_text}")
                logger.debug(f"   Full data: {report_data}")
                
            elif status == "failed":
                logger.error(f"❌ ORDER FAILED - {cl_id}")
                logger.error(f"   Reason: {reason_code} - {reason_text}")
                logger.debug(f"   Full data: {report_data}")
                
            else:
                # Log unknown status types with full data
                logger.warning(f"Unknown order status: {status}")
                logger.info(f"   Client ID: {cl_id}")
                logger.info(f"   Full data: {json.dumps(report_data, indent=2)}")
                
        except Exception as e:
            logger.error(f"Error processing report: {e}")
            logger.error(f"   Raw data: {json.dumps(report_data, indent=2)}")
    
    def run(self):
        """Main strategy loop"""
        logger.info(f"Strategy starting - Listening for {self.symbol} trades")
        
        last_stats_time = time.time()
        stats_interval = 30  # Print stats every 30 seconds
        
        try:
            while True:
                # Check for new trades (multipart message: topic + data)
                try:
                    # Receive multipart message: [topic, json_data]
                    topic = self.market_sub.recv_string(flags=zmq.NOBLOCK)
                    message = self.market_sub.recv_json()
                    self.process_trade(message)
                except zmq.error.Again:
                    pass
                except json.JSONDecodeError as e:
                    logger.error(f"Invalid JSON in market data: {e}")
                except Exception as e:
                    logger.error(f"Error processing market data: {e}")
                
                # Check for order reports
                try:
                    # Try to receive as single-part JSON first
                    report = self.report_sub.recv_json(flags=zmq.NOBLOCK)
                    self.process_report(report)
                except zmq.error.Again:
                    pass
                except json.JSONDecodeError as e:
                    # Might be empty frame or multipart - log and skip
                    logger.debug(f"Invalid JSON in order report (might be empty frame): {e}")
                except Exception as e:
                    logger.error(f"Error processing order report: {e}")
                
                # Print periodic stats
                current_time = time.time()
                if current_time - last_stats_time > stats_interval:
                    momentum = self.calculate_momentum() if len(self.trade_prices) >= 2 else 0.0
                    logger.info(f"📊 Stats - Position: {self.current_position:.4f} | "
                               f"Price: ${self.last_price:.2f} | "
                               f"Momentum: {momentum:.4f} | "
                               f"Orders: {self.orders_sent}")
                    last_stats_time = current_time
                
                time.sleep(0.001)  # 1ms sleep to prevent busy loop
                
        except KeyboardInterrupt:
            logger.info("Strategy shutting down...")
        finally:
            self.cleanup()
    
    def cleanup(self):
        """Cleanup ZMQ connections"""
        self.market_sub.close()
        self.order_client.close()
        self.report_sub.close()
        self.context.term()
        logger.info("Strategy cleanup complete")

def main():
    # Parse command line args
    import argparse
    parser = argparse.ArgumentParser(description="Simple Momentum Strategy")
    parser.add_argument("--symbol", default="BTC", help="Trading symbol (default: BTC)")
    parser.add_argument("--size", type=float, default=0.001, help="Position size (default: 0.001)")
    parser.add_argument("--max-position", type=float, default=0.01, help="Max position (default: 0.01)")
    parser.add_argument("--window", type=int, default=20, help="Momentum window (default: 20)")
    parser.add_argument("--threshold", type=float, default=0.0005, help="Momentum threshold (default: 0.0005)")
    parser.add_argument("--log-level", default="INFO", 
                       choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                       help="Logging level (default: INFO)")
    
    args = parser.parse_args()
    
    # Set log level from command line
    logger.setLevel(getattr(logging, args.log_level))
    
    # Create and run strategy
    strategy = MomentumStrategy(
        symbol=args.symbol,
        position_size=args.size,
        max_position=args.max_position,
        momentum_window=args.window,
        momentum_threshold=args.threshold
    )
    
    strategy.run()

if __name__ == "__main__":
    main()
