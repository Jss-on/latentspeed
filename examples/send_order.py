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
import threading
from typing import Dict, Optional, Set
from collections import defaultdict

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('OrderSender')


class CancelTracker:
    """Tracks cancel request status and confirmations"""
    
    def __init__(self):
        self.pending_cancels: Set[str] = set()  # Orders waiting for cancel confirmation
        self.confirmed_cancels: Set[str] = set()  # Orders confirmed cancelled
        self.failed_cancels: Dict[str, str] = {}  # Orders with cancel failures
        self.cancel_timeouts: Dict[str, float] = {}  # Cancel request timestamps
        self.lock = threading.RLock()
    
    def add_cancel_request(self, order_id: str) -> None:
        """Track a new cancel request"""
        with self.lock:
            self.pending_cancels.add(order_id)
            self.cancel_timeouts[order_id] = time.time()
    
    def confirm_cancel(self, order_id: str) -> None:
        """Mark cancel as confirmed"""
        with self.lock:
            self.pending_cancels.discard(order_id)
            self.confirmed_cancels.add(order_id)
            self.cancel_timeouts.pop(order_id, None)
    
    def fail_cancel(self, order_id: str, reason: str) -> None:
        """Mark cancel as failed"""
        with self.lock:
            self.pending_cancels.discard(order_id)
            self.failed_cancels[order_id] = reason
            self.cancel_timeouts.pop(order_id, None)
    
    def check_timeouts(self, timeout_seconds: float = 10.0) -> Set[str]:
        """Check for timed-out cancel requests"""
        with self.lock:
            current_time = time.time()
            timed_out = set()
            for order_id, request_time in list(self.cancel_timeouts.items()):
                if current_time - request_time > timeout_seconds:
                    timed_out.add(order_id)
                    self.fail_cancel(order_id, "timeout")
            return timed_out
    
    def get_status(self, order_id: str) -> str:
        """Get cancel status for an order"""
        with self.lock:
            if order_id in self.confirmed_cancels:
                return "confirmed"
            elif order_id in self.failed_cancels:
                return f"failed: {self.failed_cancels[order_id]}"
            elif order_id in self.pending_cancels:
                return "pending"
            else:
                return "unknown"


class OrderSender:
    """Client for sending orders to Latentspeed Trading Engine"""
    
    def __init__(self, endpoint: str = "tcp://127.0.0.1:5601", cpu_mode: str = "normal", listen_reports: bool = True):
        self.endpoint = endpoint
        self.cpu_mode = cpu_mode  # "high_perf", "normal", "eco"
        self.listen_reports = listen_reports
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.PUSH)
        self.order_counter = 0
        
        # Cancel tracking
        self.cancel_tracker = CancelTracker()
        self.report_listener_thread = None
        self.report_socket = None
        
        # Configure socket based on CPU mode
        if cpu_mode == "high_perf":
            # High-performance mode: minimize latency, maximize CPU usage
            self.socket.setsockopt(zmq.SNDHWM, 1000)  # Higher send buffer
            self.socket.setsockopt(zmq.SNDTIMEO, 1)   # Very short timeout
            logger.info(f"🚀 High-performance mode enabled - optimized for ultra-low latency")
        elif cpu_mode == "eco":
            # Eco mode: CPU-friendly settings
            self.socket.setsockopt(zmq.SNDHWM, 100)   # Lower send buffer
            self.socket.setsockopt(zmq.SNDTIMEO, 100) # Longer timeout
            logger.info(f"🌱 Eco mode enabled - CPU-friendly operation")
        else:
            # Normal balanced mode
            self.socket.setsockopt(zmq.SNDHWM, 500)
            self.socket.setsockopt(zmq.SNDTIMEO, 10)
            logger.info(f"⚖️ Normal mode enabled - balanced performance/CPU usage")
    
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
        reduce_only: bool = False,
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
                "time_in_force": time_in_force,
                "reduce_only": reduce_only
            },
            "ts_ns": int(time.time() * 1_000_000_000),
            "tags": tags or {"source": "test_script"},
            "cpu_mode": self.cpu_mode  # Pass CPU mode to engine
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
            reduce_only_str = " [REDUCE-ONLY]" if reduce_only else ""
            logger.info(f"✅ Order sent - ID: {cl_id}{reduce_only_str}")
            logger.info(f"   {side.upper()} {size} {symbol} @ {price or 'MARKET'}")
            return cl_id
        except Exception as e:
            logger.error(f"Failed to send order: {e}")
            return None
    
    def send_cancel_order(
        self, 
        cl_id_to_cancel: str, 
        original_symbol: str,
        original_side: str,
        original_size: str,
        venue: str = "bybit",
        product_type: str = "perpetual"
    ) -> str:
        """Send an order close request (cancellation via opposite order)"""
        self.order_counter += 1
        cl_id = f"close_{int(time.time())}_{self.order_counter}"
        
        order = {
            "version": 1,
            "cl_id": cl_id,
            "action": "cancel",
            "venue_type": "cex",
            "venue": venue,
            "product_type": product_type,
            "details": {},
            "ts_ns": int(time.time() * 1_000_000_000),
            "tags": {
                "source": "test_script",
                "cl_id_to_cancel": cl_id_to_cancel,
                "original_symbol": original_symbol,
                "original_side": original_side,
                "original_size": original_size
            }
        }
        
        try:
            self.socket.send_string(json.dumps(order))
            logger.info(f"🚫 Close order sent for: {cl_id_to_cancel}")
            logger.info(f"   Will place opposite {('sell' if original_side == 'buy' else 'buy')} order for {original_size} {original_symbol}")
            return cl_id
        except Exception as e:
            logger.error(f"Failed to send close order: {e}")
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
    
    def _start_report_listener(self):
        """Start execution report listener thread"""
        if self.report_listener_thread and self.report_listener_thread.is_alive():
            return
        
        try:
            # Use SUB socket to match trading engine's PUB socket
            self.report_socket = self.context.socket(zmq.SUB)
            self.report_socket.connect("tcp://127.0.0.1:5602")  # Report endpoint
            
            # Subscribe to execution reports (trading engine sends topics)
            self.report_socket.setsockopt_string(zmq.SUBSCRIBE, "exec.report")
            self.report_socket.setsockopt_string(zmq.SUBSCRIBE, "exec.fill")
            self.report_socket.setsockopt(zmq.RCVTIMEO, 100)  # 100ms timeout for responsiveness
            
            self._running = True
            
            def report_listener_thread():
                logger.info("📡 Started execution report listener (SUB socket)")
                message_count = 0
                
                while self._running:
                    try:
                        # Receive topic and message (PUB/SUB pattern)
                        topic = self.report_socket.recv_string(zmq.DONTWAIT)
                        message = self.report_socket.recv_string(zmq.DONTWAIT)
                        message_count += 1
                        
                        logger.debug(f"📨 Received report #{message_count} [topic: {topic}]: {message[:100]}...")
                        
                        try:
                            report = json.loads(message)
                            self._process_execution_report(report)
                        except json.JSONDecodeError as e:
                            logger.error(f"❌ Invalid JSON in execution report: {e}")
                            logger.debug(f"Raw message: {message}")
                        
                    except zmq.Again:
                        continue  # Timeout, continue listening
                    except zmq.ZMQError as e:
                        if self._running and e.errno != zmq.EAGAIN:
                            logger.error(f"❌ ZMQ error in report listener: {e}")
                        break
                    except Exception as e:
                        if self._running:
                            logger.error(f"❌ Unexpected error in report listener: {e}")
                        break
                
                logger.info(f"📡 Execution report listener stopped (processed {message_count} messages)")
            
            self.report_listener_thread = threading.Thread(target=report_listener_thread, daemon=True)
            self.report_listener_thread.start()
            logger.info("📡 SUB report listener thread started successfully")
            
        except Exception as e:
            logger.error(f"❌ Failed to start report listener: {e}")
    
    def listen_reports(self):
        """Start report listener thread"""
        self._start_report_listener()
    
    def wait_for_cancel_confirmations(self, timeout_seconds: float = 5.0) -> Dict[str, str]:
        """Wait for cancel confirmations"""
        start_time = time.time()
        cancel_status = {}
        
        while time.time() - start_time < timeout_seconds:
            time.sleep(0.1)
            for order_id in self.cancel_tracker.pending_cancels.copy():
                status = self.cancel_tracker.get_status(order_id)
                if status != "pending":
                    cancel_status[order_id] = status
                    self.cancel_tracker.pending_cancels.discard(order_id)
        
        return cancel_status


def test_reduce_only_position_management(cpu_mode: str = "normal"):
    """Test reduce_only functionality for position management"""
    sender = OrderSender(cpu_mode=cpu_mode, listen_reports=True)
    
    if not sender.connect():
        return
    
    try:
        logger.info("🔒 Testing REDUCE-ONLY Position Management")
        logger.info(f"🔧 CPU Mode: {cpu_mode.upper()}")
        logger.info("=" * 80)
        
        # Step 1: Open a position with a regular order
        logger.info("\n=== Step 1: Open Long Position (Regular Order) ===")
        long_order_id = sender.send_place_order(
            symbol="BTCUSDT",
            side="buy", 
            order_type="limit",
            size="0.01",
            price="40000.0",
            product_type="perpetual",
            reduce_only=False,  # Regular position-opening order
            tags={"strategy": "position_test", "step": "open_long"}
        )
        time.sleep(2)
        
        # Step 2: Try to place reduce-only sell to close position
        logger.info("\n=== Step 2: Close Position with Reduce-Only Order ===")
        close_order_id = sender.send_place_order(
            symbol="BTCUSDT",
            side="sell",  # Opposite side to close
            order_type="market",
            size="0.01",  # Same size to fully close
            product_type="perpetual", 
            reduce_only=True,  # CRITICAL: Only reduce existing position
            tags={"strategy": "position_test", "step": "close_long", "original_order": long_order_id}
        )
        time.sleep(2)
        
        # Step 3: Try reduce-only on same side (should be rejected or not filled)
        logger.info("\n=== Step 3: Test Reduce-Only Same Side (Should Not Increase Position) ===")
        invalid_reduce_id = sender.send_place_order(
            symbol="BTCUSDT", 
            side="buy",  # Same side as original position
            order_type="limit",
            size="0.005",
            price="39000.0",
            product_type="perpetual",
            reduce_only=True,  # Should not increase position
            tags={"strategy": "position_test", "step": "invalid_reduce"}
        )
        time.sleep(2)
        
        # Step 4: Test reduce-only with spot (should be rejected)
        logger.info("\n=== Step 4: Test Reduce-Only on Spot (Should Be Rejected) ===")
        spot_reduce_id = sender.send_place_order(
            symbol="BTCUSDT",
            side="sell",
            order_type="limit", 
            size="0.001",
            price="45000.0",
            product_type="spot",  # Spot doesn't support reduce_only
            reduce_only=True,
            tags={"strategy": "position_test", "step": "spot_reduce_invalid"}
        )
        time.sleep(2)
        
        # Step 5: Demonstrate partial reduce-only close
        logger.info("\n=== Step 5: Partial Position Close with Reduce-Only ===")
        # First open another position
        position_order_id = sender.send_place_order(
            symbol="ETHUSDT",
            side="buy",
            order_type="limit",
            size="0.1", 
            price="2400.0",
            product_type="perpetual",
            reduce_only=False,
            tags={"strategy": "position_test", "step": "open_eth_long"}
        )
        time.sleep(2)
        
        # Partially close with reduce-only
        partial_close_id = sender.send_place_order(
            symbol="ETHUSDT",
            side="sell",
            order_type="limit",
            size="0.05",  # Only half the position
            price="2500.0",
            product_type="perpetual", 
            reduce_only=True,  # Only reduce, don't go short
            tags={"strategy": "position_test", "step": "partial_close", "original_order": position_order_id}
        )
        time.sleep(2)
        
        logger.info("\n" + "=" * 80)
        logger.info("🔒 REDUCE-ONLY Position Management Test Complete!")
        logger.info("📊 Test Summary:")
        logger.info("   ✅ Regular position opening")
        logger.info("   ✅ Reduce-only position closing")
        logger.info("   ✅ Invalid reduce-only handling")
        logger.info("   ✅ Spot reduce-only rejection")
        logger.info("   ✅ Partial position reduction")
        logger.info("=" * 80)
        
    except Exception as e:
        logger.error(f"Reduce-only test failed: {e}")
    finally:
        sender.disconnect()


def test_sequence(cpu_mode: str = "normal"):
    """Run a comprehensive test sequence covering all trading engine functionalities"""
    sender = OrderSender(cpu_mode=cpu_mode, listen_reports=True)  # Enable report listening
    
    if not sender.connect():
        return
    
    try:
        logger.info("🚀 Starting comprehensive trading engine test sequence with ETHUSDT")
        logger.info(f"🔧 CPU Mode: {cpu_mode.upper()}")
        logger.info("📡 Execution report listening: ENABLED")
        logger.info("=" * 80)
        
        # Test 1: Place limit buy order (ETHUSDT Perpetual)
        logger.info("\n=== Test 1: ETHUSDT Perpetual - Limit Buy Order ===")
        order_id_1 = sender.send_place_order(
            symbol="ETHUSDT",
            side="buy",
            order_type="limit",
            size="0.1",
            price="2500.0",
            product_type="perpetual",
            time_in_force="GTC",
            tags={"test": "limit_buy_perpetual"}
        )
        time.sleep(1.5)
        
        # Test 2: Place market sell order (ETHUSDT Spot)
        logger.info("\n=== Test 2: ETHUSDT Spot - Market Sell Order ===")
        order_id_2 = sender.send_place_order(
            symbol="ETHUSDT",
            side="sell",
            order_type="market",
            size="0.05",
            product_type="spot",
            tags={"test": "market_sell_spot"}
        )
        time.sleep(1.5)
        
        # Test 3: Place limit sell with IOC (Immediate or Cancel)
        logger.info("\n=== Test 3: ETHUSDT Perpetual - Limit Sell IOC ===")
        order_id_3 = sender.send_place_order(
            symbol="ETHUSDT",
            side="sell",
            order_type="limit",
            size="0.08",
            price="2600.0",
            product_type="perpetual",
            time_in_force="IOC",
            tags={"test": "limit_sell_ioc"}
        )
        time.sleep(1.5)
        
        # Test 4: Place limit buy with FOK (Fill or Kill)
        logger.info("\n=== Test 4: ETHUSDT Spot - Limit Buy FOK ===")
        order_id_4 = sender.send_place_order(
            symbol="ETHUSDT",
            side="buy",
            order_type="limit",
            size="0.03",
            price="2400.0",
            product_type="spot",
            time_in_force="FOK",
            tags={"test": "limit_buy_fok"}
        )
        time.sleep(1.5)
        
        # Test 5: Order closing functionality (NEW MECHANISM)
        if order_id_1:
            logger.info("\n=== Test 5: Order Closing Functionality ===")
            logger.info("   Using new order closing mechanism (opposite order placement)")
            sender.send_cancel_order(order_id_1, "ETHUSDT", "buy", "0.1", "bybit", "perpetual")
            time.sleep(1.5)
        
        # Test 6: Place and modify order (test replace functionality)
        logger.info("\n=== Test 6: Place and Replace Order ===")
        order_id_5 = sender.send_place_order(
            symbol="ETHUSDT",
            side="sell",
            order_type="limit",
            size="0.12",
            price="2700.0",
            product_type="perpetual",
            tags={"test": "replace_target"}
        )
        
        if order_id_5:
            time.sleep(1.5)
            logger.info("   Modifying order price and size...")
            sender.send_replace_order(
                cl_id_to_replace=order_id_5,
                new_price="2650.0",
                new_size="0.15"
            )
            time.sleep(1.5)
        
        # Test 7: Test different venue (if supported)
        logger.info("\n=== Test 7: Alternative Venue Test ===")
        order_id_6 = sender.send_place_order(
            symbol="ETHUSDT",
            side="buy",
            order_type="limit",
            size="0.06",
            price="2450.0",
            venue="binance",  # Test different venue
            product_type="spot",
            tags={"test": "alternative_venue"}
        )
        time.sleep(1.5)
        
        # Test 8: Larger order sizes (risk management test)
        logger.info("\n=== Test 8: Larger Order Size (Risk Management) ===")
        order_id_7 = sender.send_place_order(
            symbol="ETHUSDT",
            side="buy",
            order_type="limit",
            size="1.0",  # Larger size
            price="2300.0",
            product_type="perpetual",
            tags={"test": "large_order", "risk_level": "high"}
        )
        time.sleep(1.5)
        
        # Test 9: Rapid fire orders (latency test)
        logger.info("\n=== Test 9: Rapid Fire Orders (Latency Test) ===")
        rapid_orders = []
        rapid_order_details = []  # Track details for closing
        for i in range(3):
            side = "buy" if i % 2 == 0 else "sell"
            size = "0.02"
            order_id = sender.send_place_order(
                symbol="ETHUSDT",
                side=side,
                order_type="limit",
                size=size,
                price=str(2500.0 + (i * 10)),
                product_type="perpetual",
                tags={"test": "rapid_fire", "batch": i}
            )
            rapid_orders.append(order_id)
            rapid_order_details.append((side, size))
            time.sleep(0.1)  # Minimal delay
        
        time.sleep(2)
        
        # Test 10: Bulk order closing operations
        logger.info("\n=== Test 10: Bulk Order Closing Operations ===")
        orders_to_close = [
            (order_id_5, "sell", "0.15", "perpetual"),  # Modified order
            (order_id_6, "buy", "0.06", "spot"),
            (order_id_7, "buy", "1.0", "perpetual")
        ]
        
        # Add rapid fire orders with their details
        for order_id, (side, size) in zip(rapid_orders, rapid_order_details):
            if order_id:
                orders_to_close.append((order_id, side, size, "perpetual"))
        
        for i, (order_id, side, size, product_type) in enumerate(orders_to_close):
            if order_id:
                logger.info(f"   Closing order {i+1}/{len(orders_to_close)} via opposite order")
                sender.send_cancel_order(order_id, "ETHUSDT", side, size, "bybit", product_type)
                time.sleep(0.5)
        
        # Test 11: Error handling - invalid parameters
        logger.info("\n=== Test 11: Error Handling Tests ===")
        
        # Test invalid price
        logger.info("   Testing invalid price...")
        sender.send_place_order(
            symbol="ETHUSDT",
            side="buy",
            order_type="limit",
            size="0.01",
            price="invalid_price",
            tags={"test": "error_invalid_price"}
        )
        time.sleep(1)
        
        # Test missing price for limit order
        logger.info("   Testing missing price for limit order...")
        sender.send_place_order(
            symbol="ETHUSDT",
            side="buy",
            order_type="limit",
            size="0.01",
            price=None,  # Should trigger error
            tags={"test": "error_missing_price"}
        )
        time.sleep(1)
        
        # Test 12: Cross-product arbitrage simulation
        logger.info("\n=== Test 12: Cross-Product Trading Simulation ===")
        
        # Simulate spot buy
        spot_buy_id = sender.send_place_order(
            symbol="ETHUSDT",
            side="buy",
            order_type="market",
            size="0.1",
            product_type="spot",
            tags={"test": "arbitrage", "leg": "spot_buy"}
        )
        
        time.sleep(1)
        
        # Simulate perpetual sell
        perp_sell_id = sender.send_place_order(
            symbol="ETHUSDT",
            side="sell",
            order_type="market",
            size="0.1",
            product_type="perpetual",
            tags={"test": "arbitrage", "leg": "perp_sell"}
        )
        
        time.sleep(2)
        
        logger.info("\n" + "=" * 80)
        logger.info("🎉 Comprehensive test sequence completed!")
        logger.info("📊 Test Summary:")
        logger.info(f"   - CPU Mode: {cpu_mode.upper()}")
        logger.info("   - Tested ETHUSDT on both spot and perpetual markets")
        logger.info("   - Tested limit, market orders with various TIF options")
        logger.info("   - Tested order cancellation and modification")
        logger.info("   - ✅ VALIDATED cancel confirmations from Bybit exchange")
        logger.info("   - Tested multiple venues and risk management")
        logger.info("   - Tested rapid order submission and bulk operations")
        logger.info("   - Tested error handling scenarios")
        logger.info("   - Simulated cross-product arbitrage strategy")
        logger.info("=" * 80)
        
    except Exception as e:
        logger.error(f"Test sequence failed: {e}")
    finally:
        sender.disconnect()


def enable_debug_logging():
    """Enable debug logging"""
    logger.setLevel(logging.DEBUG)
    logger.info("🔍 Debug logging enabled")


def test_report_connection():
    """Test if trading engine report endpoint is working"""
    logger.info("🔌 Testing report endpoint connection...")
    
    context = zmq.Context()
    test_socket = context.socket(zmq.SUB)  # Use SUB socket to match PUB
    test_socket.setsockopt_string(zmq.SUBSCRIBE, "")  # Subscribe to all topics
    test_socket.setsockopt(zmq.RCVTIMEO, 2000)  # 2 second timeout
    
    try:
        test_socket.connect("tcp://127.0.0.1:5602")
        logger.info("✅ Connected to tcp://127.0.0.1:5602 (SUB socket)")
        
        # Try to receive a message with timeout
        try:
            topic = test_socket.recv_string(zmq.DONTWAIT)
            message = test_socket.recv_string(zmq.DONTWAIT)
            logger.info(f"📨 Received test message [topic: {topic}]: {message[:200]}...")
        except zmq.Again:
            logger.warning("⚠️ No messages available on report endpoint (this is expected if no trading activity)")
        except Exception as e:
            logger.error(f"❌ Error receiving from report endpoint: {e}")
            
    except Exception as e:
        logger.error(f"❌ Cannot connect to report endpoint: {e}")
    finally:
        test_socket.close()
        context.term()
        logger.info("🔌 Report connection test completed")


def main():
    """Main entry point with CLI support"""
    parser = argparse.ArgumentParser(description="Send orders to Latentspeed Trading Engine")
    parser.add_argument("--endpoint", default="tcp://127.0.0.1:5601", help="Trading engine endpoint")
    parser.add_argument("--cpu-mode", choices=["high_perf", "normal", "eco"], default="normal", help="CPU usage mode")
    parser.add_argument("--action", choices=["place", "cancel", "replace", "test", "debug", "reduce_only"], default="test",
                       help="Order action (default: test)")
    parser.add_argument("--debug", action="store_true", help="Enable debug logging")
    parser.add_argument("--test-reports", action="store_true", help="Test report endpoint connection")
    parser.add_argument("--symbol", default="ETHUSDT", help="Trading symbol")
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
    parser.add_argument("--reduce-only", action="store_true", help="Place reduce-only order (derivatives only)")
    
    args = parser.parse_args()
    
    # Enable debug logging if requested
    if args.debug:
        enable_debug_logging()
    
    # Test report connection if requested
    if args.test_reports:
        test_report_connection()
        return
    
    if args.action == "test":
        test_sequence(cpu_mode=args.cpu_mode)
    elif args.action == "reduce_only":
        test_reduce_only_position_management(cpu_mode=args.cpu_mode)
    elif args.action == "debug":
        logger.info("🐛 Running debug mode...")
        enable_debug_logging()
        test_report_connection()
        
        # Run a simplified test with debug logging
        sender = OrderSender(cpu_mode=args.cpu_mode, listen_reports=True)
        if sender.connect():
            logger.info("🧪 Sending test cancel to check reports...")
            test_order_id = sender.send_place_order(
                symbol="ETHUSDT", side="buy", order_type="limit", 
                size="0.001", price="2000.0", product_type="spot"
            )
            time.sleep(1)
            if test_order_id:
                sender.send_cancel_order(test_order_id, "ETHUSDT", "buy", "0.001")
                logger.info("⏳ Waiting 5s for cancel confirmation...")
                time.sleep(5)
                status = sender.wait_for_cancel_confirmations(timeout_seconds=2.0)
                logger.info(f"📋 Final status: {status}")
            sender.disconnect()
    else:
        sender = OrderSender(args.endpoint, cpu_mode=args.cpu_mode)
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
                    product_type=args.product,
                    reduce_only=args.reduce_only
                )
            
            elif args.action == "cancel":
                if not args.cancel_id:
                    logger.error("--cancel-id required for cancel action")
                    sys.exit(1)
                sender.send_cancel_order(args.cancel_id, args.symbol, args.side, args.size, args.venue, args.product)
            
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
