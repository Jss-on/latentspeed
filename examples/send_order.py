#!/usr/bin/env python3
"""
Simple order sender for testing Latentspeed Trading Engine
Supports multiple order types and actions
"""

import zmq
import json
import time
import sys
import argparse
import logging
from typing import Dict, Optional

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('OrderSender')


class OrderSender:
    """Client for sending orders to Latentspeed Trading Engine"""
    
    def __init__(self, endpoint: str = "tcp://127.0.0.1:5601"):
        self.endpoint = endpoint
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.PUSH)
        self.order_counter = 0
        
    def connect(self) -> bool:
        """Connect to trading engine"""
        try:
            self.socket.connect(self.endpoint)
            logger.info(f"Connected to trading engine at {self.endpoint}")
            return True
        except Exception as e:
            logger.error(f"Failed to connect: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from trading engine"""
        self.socket.close()
        self.context.term()
        logger.info("Disconnected from trading engine")
    
    def send_place_order(
        self,
        symbol: str,
        side: str,
        order_type: str,
        size: str,
        price: Optional[str] = None,
        venue: str = "bybit",
        product_type: str = "perpetual",
        time_in_force: str = "GTC",
        tags: Optional[Dict] = None
    ) -> str:
        """Send a place order request"""
        self.order_counter += 1
        cl_id = f"test_order_{int(time.time())}_{self.order_counter}"
        
        order = {
            "version": 1,
            "cl_id": cl_id,
            "action": "place",
            "venue_type": "cex",
            "venue": venue,
            "product_type": product_type,
            "details": {
                "symbol": symbol,
                "side": side,
                "order_type": order_type,
                "size": size,
                "time_in_force": time_in_force
            },
            "ts_ns": int(time.time() * 1_000_000_000),
            "tags": tags or {"source": "test_script"}
        }
        
        # Add price for limit orders
        if order_type == "limit" and price:
            order["details"]["price"] = price
        elif order_type == "limit":
            logger.error("Price required for limit orders")
            return None
        
        # Send order
        try:
            self.socket.send_string(json.dumps(order))
            logger.info(f"✅ Order sent - ID: {cl_id}")
            logger.info(f"   {side.upper()} {size} {symbol} @ {price or 'MARKET'}")
            return cl_id
        except Exception as e:
            logger.error(f"Failed to send order: {e}")
            return None
    
    def send_cancel_order(self, cl_id_to_cancel: str, venue: str = "bybit") -> str:
        """Send a cancel order request"""
        self.order_counter += 1
        cl_id = f"cancel_{int(time.time())}_{self.order_counter}"
        
        order = {
            "version": 1,
            "cl_id": cl_id,
            "action": "cancel",
            "venue_type": "cex",
            "venue": venue,
            "product_type": "perpetual",
            "details": {
                "cl_id_to_cancel": cl_id_to_cancel
            },
            "ts_ns": int(time.time() * 1_000_000_000),
            "tags": {"source": "test_script"}
        }
        
        try:
            self.socket.send_string(json.dumps(order))
            logger.info(f"🚫 Cancel sent for order: {cl_id_to_cancel}")
            return cl_id
        except Exception as e:
            logger.error(f"Failed to send cancel: {e}")
            return None
    
    def send_replace_order(
        self,
        cl_id_to_replace: str,
        new_size: Optional[str] = None,
        new_price: Optional[str] = None,
        venue: str = "bybit"
    ) -> str:
        """Send a replace/modify order request"""
        if not new_size and not new_price:
            logger.error("At least one of new_size or new_price required for replace")
            return None
        
        self.order_counter += 1
        cl_id = f"replace_{int(time.time())}_{self.order_counter}"
        
        order = {
            "version": 1,
            "cl_id": cl_id,
            "action": "replace",
            "venue_type": "cex",
            "venue": venue,
            "product_type": "perpetual",
            "details": {
                "cl_id_to_replace": cl_id_to_replace
            },
            "ts_ns": int(time.time() * 1_000_000_000),
            "tags": {"source": "test_script"}
        }
        
        if new_size:
            order["details"]["new_size"] = new_size
        if new_price:
            order["details"]["new_price"] = new_price
        
        try:
            self.socket.send_string(json.dumps(order))
            logger.info(f"🔄 Replace sent for order: {cl_id_to_replace}")
            if new_size:
                logger.info(f"   New size: {new_size}")
            if new_price:
                logger.info(f"   New price: {new_price}")
            return cl_id
        except Exception as e:
            logger.error(f"Failed to send replace: {e}")
            return None


def test_sequence():
    """Run a test sequence of orders"""
    sender = OrderSender()
    
    if not sender.connect():
        return
    
    try:
        # Test 1: Place a limit buy order
        logger.info("\n=== Test 1: Place Limit Buy Order ===")
        order_id_1 = sender.send_place_order(
            symbol="BTCUSDT",
            side="buy",
            order_type="limit",
            size="0.001",
            price="50000.0",
            product_type="perpetual"
        )
        time.sleep(2)
        
        # Test 2: Place a market sell order
        logger.info("\n=== Test 2: Place Market Sell Order ===")
        order_id_2 = sender.send_place_order(
            symbol="ETHUSDT",
            side="sell",
            order_type="market",
            size="0.01",
            product_type="spot"
        )
        time.sleep(2)
        
        # Test 3: Cancel the first order
        if order_id_1:
            logger.info("\n=== Test 3: Cancel First Order ===")
            sender.send_cancel_order(order_id_1)
            time.sleep(2)
        
        # Test 4: Place and modify an order
        logger.info("\n=== Test 4: Place and Modify Order ===")
        order_id_3 = sender.send_place_order(
            symbol="BTCUSDT",
            side="sell",
            order_type="limit",
            size="0.002",
            price="55000.0",
            product_type="perpetual"
        )
        
        if order_id_3:
            time.sleep(2)
            sender.send_replace_order(
                cl_id_to_replace=order_id_3,
                new_price="54000.0",
                new_size="0.003"
            )
        
    finally:
        sender.disconnect()


def main():
    """Main entry point with CLI support"""
    parser = argparse.ArgumentParser(description="Send orders to Latentspeed Trading Engine")
    parser.add_argument("--endpoint", default="tcp://127.0.0.1:5601", help="Trading engine endpoint")
    parser.add_argument("--action", choices=["place", "cancel", "replace", "test"], default="test",
                       help="Order action (default: test)")
    parser.add_argument("--symbol", default="BTCUSDT", help="Trading symbol")
    parser.add_argument("--side", choices=["buy", "sell"], default="buy", help="Order side")
    parser.add_argument("--type", choices=["limit", "market"], default="limit", help="Order type")
    parser.add_argument("--size", default="0.001", help="Order size")
    parser.add_argument("--price", help="Order price (required for limit orders)")
    parser.add_argument("--product", choices=["spot", "perpetual"], default="perpetual",
                       help="Product type")
    parser.add_argument("--venue", default="bybit", help="Exchange venue")
    parser.add_argument("--cancel-id", help="Order ID to cancel")
    parser.add_argument("--replace-id", help="Order ID to replace")
    parser.add_argument("--new-size", help="New size for replace")
    parser.add_argument("--new-price", help="New price for replace")
    
    args = parser.parse_args()
    
    if args.action == "test":
        test_sequence()
    else:
        sender = OrderSender(args.endpoint)
        if not sender.connect():
            sys.exit(1)
        
        try:
            if args.action == "place":
                if args.type == "limit" and not args.price:
                    logger.error("Price required for limit orders")
                    sys.exit(1)
                
                sender.send_place_order(
                    symbol=args.symbol,
                    side=args.side,
                    order_type=args.type,
                    size=args.size,
                    price=args.price,
                    venue=args.venue,
                    product_type=args.product
                )
            
            elif args.action == "cancel":
                if not args.cancel_id:
                    logger.error("--cancel-id required for cancel action")
                    sys.exit(1)
                sender.send_cancel_order(args.cancel_id, args.venue)
            
            elif args.action == "replace":
                if not args.replace_id:
                    logger.error("--replace-id required for replace action")
                    sys.exit(1)
                if not args.new_size and not args.new_price:
                    logger.error("At least one of --new-size or --new-price required")
                    sys.exit(1)
                sender.send_replace_order(
                    args.replace_id,
                    args.new_size,
                    args.new_price,
                    args.venue
                )
        
        finally:
            sender.disconnect()


if __name__ == "__main__":
    main()
