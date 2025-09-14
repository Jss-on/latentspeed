#!/usr/bin/env python3
"""
Complete Python Trading Client Example
Demonstrates full integration with TradingEngineService via ZeroMQ
"""

import zmq
import json
import time
import threading
import uuid
from typing import Dict, Any, Optional

class TradingClient:
    def __init__(self, order_endpoint="tcp://127.0.0.1:5601", 
                 report_endpoint="tcp://127.0.0.1:5602"):
        """Initialize trading client with ZeroMQ connections"""
        self.context = zmq.Context()
        
        # Order sender (PUSH socket)
        self.order_socket = self.context.socket(zmq.PUSH)
        self.order_socket.connect(order_endpoint)
        
        # Report receiver (SUB socket)
        self.report_socket = self.context.socket(zmq.SUB)
        self.report_socket.connect(report_endpoint)
        
        # Subscribe to all execution reports and fills
        self.report_socket.setsockopt(zmq.SUBSCRIBE, b"exec.report")
        self.report_socket.setsockopt(zmq.SUBSCRIBE, b"exec.fill")
        
        # Order tracking
        self.pending_orders: Dict[str, Dict[str, Any]] = {}
        self.reports: Dict[str, Dict[str, Any]] = {}
        self.fills: Dict[str, list] = {}
        
        # Start report listener thread
        self.running = True
        self.listener_thread = threading.Thread(target=self._report_listener)
        self.listener_thread.daemon = True
        self.listener_thread.start()
        
        print("Trading client initialized")
        print(f"Orders: {order_endpoint}")
        print(f"Reports: {report_endpoint}")

    def _report_listener(self):
        """Background thread to receive execution reports and fills"""
        while self.running:
            try:
                # Non-blocking receive with timeout
                if self.report_socket.poll(100):  # 100ms timeout
                    topic = self.report_socket.recv_string(zmq.NOBLOCK)
                    message = self.report_socket.recv_string(zmq.NOBLOCK)
                    
                    if topic == "exec.report":
                        report = json.loads(message)
                        self._handle_execution_report(report)
                    elif topic == "exec.fill":
                        fill = json.loads(message)
                        self._handle_fill(fill)
                        
            except zmq.Again:
                continue  # No message available
            except Exception as e:
                print(f"Error in report listener: {e}")

    def _handle_execution_report(self, report: Dict[str, Any]):
        """Handle incoming execution report"""
        cl_id = report.get("cl_id")
        status = report.get("status")
        reason_text = report.get("reason_text", "")
        
        self.reports[cl_id] = report
        
        print(f"📊 REPORT [{cl_id}]: {status}")
        if reason_text:
            print(f"   └─ {reason_text}")
            
        if status in ["rejected", "cancel_rejected"]:
            print(f"   └─ Reason Code: {report.get('reason_code', 'unknown')}")

    def _handle_fill(self, fill: Dict[str, Any]):
        """Handle incoming fill report"""
        cl_id = fill.get("cl_id")
        
        if cl_id not in self.fills:
            self.fills[cl_id] = []
        self.fills[cl_id].append(fill)
        
        print(f"💰 FILL [{cl_id}]: {fill.get('size')} @ {fill.get('price')}")
        print(f"   └─ Fee: {fill.get('fee_amount')} {fill.get('fee_currency')}")
        print(f"   └─ Liquidity: {fill.get('liquidity', 'unknown')}")

    def place_limit_order(self, symbol: str, side: str, size: str, price: str,
                         venue: str = "bybit", product_type: str = "spot",
                         time_in_force: str = "GTC", **kwargs) -> str:
        """Place a limit order"""
        cl_id = f"limit_{uuid.uuid4().hex[:8]}"
        
        order = {
            "version": 1,
            "cl_id": cl_id,
            "action": "place",
            "venue_type": "cex",
            "venue": venue,
            "product_type": product_type,
            "details": {
                "symbol": symbol,
                "side": side.lower(),
                "order_type": "limit",
                "size": size,
                "price": price,
                "time_in_force": time_in_force
            },
            "ts_ns": int(time.time() * 1_000_000_000),
            "tags": {
                "client": "python_example",
                "session": "demo",
                **kwargs.get("tags", {})
            }
        }
        
        self.pending_orders[cl_id] = order
        self.order_socket.send_string(json.dumps(order))
        
        print(f"📤 SENT LIMIT ORDER [{cl_id}]: {side} {size} {symbol} @ {price}")
        return cl_id

    def place_market_order(self, symbol: str, side: str, size: str,
                          venue: str = "bybit", product_type: str = "spot",
                          **kwargs) -> str:
        """Place a market order"""
        cl_id = f"market_{uuid.uuid4().hex[:8]}"
        
        order = {
            "version": 1,
            "cl_id": cl_id,
            "action": "place",
            "venue_type": "cex",
            "venue": venue,
            "product_type": product_type,
            "details": {
                "symbol": symbol,
                "side": side.lower(),
                "order_type": "market",
                "size": size
            },
            "ts_ns": int(time.time() * 1_000_000_000),
            "tags": {
                "client": "python_example",
                "session": "demo",
                **kwargs.get("tags", {})
            }
        }
        
        self.pending_orders[cl_id] = order
        self.order_socket.send_string(json.dumps(order))
        
        print(f"📤 SENT MARKET ORDER [{cl_id}]: {side} {size} {symbol}")
        return cl_id

    def cancel_order(self, cl_id_to_cancel: str, symbol: Optional[str] = None) -> str:
        """Cancel an existing order"""
        cl_id = f"cancel_{uuid.uuid4().hex[:8]}"
        
        details = {"cl_id_to_cancel": cl_id_to_cancel}
        if symbol:
            details["symbol"] = symbol
            
        cancel_order = {
            "version": 1,
            "cl_id": cl_id,
            "action": "cancel",
            "venue_type": "cex",
            "venue": "bybit",  # Should match original order venue
            "details": details,
            "ts_ns": int(time.time() * 1_000_000_000),
            "tags": {
                "client": "python_example",
                "session": "demo"
            }
        }
        
        self.order_socket.send_string(json.dumps(cancel_order))
        print(f"📤 SENT CANCEL [{cl_id}]: canceling {cl_id_to_cancel}")
        return cl_id

    def get_order_status(self, cl_id: str) -> Optional[Dict[str, Any]]:
        """Get latest status for an order"""
        return self.reports.get(cl_id)

    def get_order_fills(self, cl_id: str) -> list:
        """Get all fills for an order"""
        return self.fills.get(cl_id, [])

    def wait_for_report(self, cl_id: str, timeout: float = 10.0) -> Optional[Dict[str, Any]]:
        """Wait for execution report for a specific order"""
        start_time = time.time()
        while time.time() - start_time < timeout:
            if cl_id in self.reports:
                return self.reports[cl_id]
            time.sleep(0.1)
        return None

    def close(self):
        """Clean shutdown"""
        self.running = False
        if self.listener_thread.is_alive():
            self.listener_thread.join(timeout=1.0)
        self.order_socket.close()
        self.report_socket.close()
        self.context.term()
        print("Trading client closed")


def main():
    """Example trading session"""
    client = TradingClient()
    
    try:
        print("\n=== Example Trading Session ===\n")
        
        # Example 1: Place limit buy order
        print("1. Placing limit buy order...")
        buy_order_id = client.place_limit_order(
            symbol="ETHUSDT",
            side="buy", 
            size="0.01",
            price="2000.0"
        )
        
        # Wait for order confirmation
        report = client.wait_for_report(buy_order_id, timeout=5.0)
        if report:
            print(f"   Order status: {report['status']}")
        
        time.sleep(2)
        
        # Example 2: Cancel the order
        print("\n2. Canceling the order...")
        cancel_id = client.cancel_order(buy_order_id, symbol="ETHUSDT")
        
        # Wait for cancel confirmation
        cancel_report = client.wait_for_report(cancel_id, timeout=5.0)
        if cancel_report:
            print(f"   Cancel status: {cancel_report['status']}")
        
        time.sleep(2)
        
        # Example 3: Place market order (be careful with real money!)
        print("\n3. Placing small market buy order...")
        market_order_id = client.place_market_order(
            symbol="ETHUSDT",
            side="buy",
            size="0.001"  # Very small size for demo
        )
        
        # Wait for market order results
        market_report = client.wait_for_report(market_order_id, timeout=10.0)
        if market_report:
            print(f"   Market order status: {market_report['status']}")
            
            # Check for fills
            time.sleep(1)
            fills = client.get_order_fills(market_order_id)
            if fills:
                print(f"   Market order filled: {len(fills)} fill(s)")
        
        print("\n=== Session Complete ===")
        
        # Summary
        print(f"\nSession Summary:")
        print(f"- Total orders sent: {len(client.pending_orders)}")
        print(f"- Reports received: {len(client.reports)}")
        print(f"- Orders with fills: {len([k for k, v in client.fills.items() if v])}")
        
    except KeyboardInterrupt:
        print("\nSession interrupted by user")
    except Exception as e:
        print(f"Error in trading session: {e}")
    finally:
        client.close()


if __name__ == "__main__":
    main()
