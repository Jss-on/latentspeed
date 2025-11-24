#!/usr/bin/env python3
"""
Simple Trading Engine Test Script
Tests order submission, cancellation, and execution reports
"""

import zmq
import json
import uuid
import time
import sys
import requests


def get_hyperliquid_mark_price(symbol, testnet=True):
    base_url = "https://api.hyperliquid-testnet.xyz" if testnet else "https://api.hyperliquid.xyz"
    try:
        resp = requests.post(f"{base_url}/info", json={"type": "metaAndAssetCtxs"}, timeout=3)
        resp.raise_for_status()
        data = resp.json()
        universe = data.get("universe", [])
        asset_ctxs = data.get("assetCtxs", [])
        for idx, asset in enumerate(universe):
            if asset.get("name") == symbol:
                if idx >= len(asset_ctxs):
                    return None
                ctx = asset_ctxs[idx]
                mark_str = ctx.get("markPx") or ctx.get("midPx") or ctx.get("oraclePx")
                if mark_str is None:
                    return None
                return float(mark_str)
    except Exception:
        return None
    return None


class TradingEngineTester:
    def __init__(self):
        self.context = zmq.Context()
        
        # Send orders to trading engine
        self.order_socket = self.context.socket(zmq.PUSH)
        self.order_socket.connect("tcp://127.0.0.1:5601")
        
        # Receive execution reports
        self.report_socket = self.context.socket(zmq.SUB)
        self.report_socket.connect("tcp://127.0.0.1:5602")
        self.report_socket.setsockopt_string(zmq.SUBSCRIBE, "")
        self.report_socket.setsockopt(zmq.RCVTIMEO, 1000)
        
        self.pending_orders = {}
        print("✓ Connected to trading engine")
        print("  Orders: tcp://127.0.0.1:5601")
        print("  Reports: tcp://127.0.0.1:5602\n")
    
    def send_order(self, symbol, side, size, order_type="market", price=None):
        """Send an order to trading engine"""
        cl_id = str(uuid.uuid4())
        
        order = {
            "action": "place",
            "cl_id": cl_id,
            "venue": "hyperliquid",
            "venue_type": "cex",
            "product_type": "perpetual",
            "details": {
                "symbol": symbol,
                "side": side,
                "size": size,
                "order_type": order_type,
                "time_in_force": "GTC"
            }
        }
        
        if price:
            order["details"]["price"] = price
        
        self.order_socket.send_json(order)
        self.pending_orders[cl_id] = order
        
        print(f"📤 Sent {order_type.upper()} {side.upper()} {size} {symbol}", end="")
        if price:
            print(f" @ ${price}")
        else:
            print()
        print(f"   Order ID: {cl_id}\n")
        
        return cl_id
    
    def cancel_order(self, cl_id):
        """Cancel an order"""
        cancel = {
            "action": "cancel",
            "cl_id": str(uuid.uuid4()),
            "venue": "hyperliquid",
            "venue_type": "cex",
            "product_type": "perpetual",
            "details": {
                "cancel": {
                    "cl_id_to_cancel": cl_id
                }
            }
        }
        
        self.order_socket.send_json(cancel)
        print(f"🚫 Sent cancel for order: {cl_id}\n")
    
    def listen_reports(self, duration=5):
        """Listen for execution reports"""
        print(f"👂 Listening for reports ({duration}s)...\n")
        
        start_time = time.time()
        while time.time() - start_time < duration:
            try:
                topic = self.report_socket.recv_string(flags=zmq.NOBLOCK)
                message = self.report_socket.recv_string(flags=zmq.NOBLOCK)
                
                if topic == "exec.report":
                    report = json.loads(message)
                    cl_id = report.get("cl_id", "")[:8]
                    status = report.get("status", "")
                    tags = report.get("tags", {})
                    symbol = tags.get("symbol", "")
                    reason_code = report.get("reason_code", "")
                    reason_text = report.get("reason_text", "")
                    print(f"📊 Report: {status} - {symbol} - {cl_id}...")
                    if reason_text:
                        if status == "accepted":
                            print(f"   ✓ {reason_text}")
                        else:
                            print(f"   ❌ {reason_code}: {reason_text}")
                    
                elif topic == "exec.fill":
                    fill = json.loads(message)
                    cl_id = fill.get("cl_id", "")[:8]
                    size = fill.get("size", 0)
                    price = fill.get("price", 0)
                    symbol = fill.get("symbol", "")
                    print(f"💰 Fill: {size} {symbol} @ ${price} - {cl_id}...")
                    
            except zmq.Again:
                time.sleep(0.1)
            except Exception as e:
                print(f"⚠ Error: {e}")
        
        print()


def main():
    print("="*70)
    print("Trading Engine Test Script")
    print("="*70 + "\n")
    
    tester = TradingEngineTester()
    
    eth_mark = get_hyperliquid_mark_price("ETH") or 3500.0
    btc_mark = get_hyperliquid_mark_price("BTC") or 60000.0

    eth_buy_price = round(eth_mark * 1.01, 2)
    btc_sell_price = round(btc_mark * 0.99, 2)
    eth_sell_price = round(eth_mark * 0.99, 2)
    
    # Test 1: Limit buy order (use a reasonable placeholder price)
    print("Test 1: Limit Buy Order")
    print("-" * 40)
    cl_id1 = tester.send_order("ETH", "buy", 0.001, "limit", price=eth_buy_price)
    tester.listen_reports(3)
    
    # Test 2: Limit sell order
    print("Test 2: Limit Sell Order")
    print("-" * 40)
    cl_id2 = tester.send_order("BTC", "sell", 0.0001, "limit", price=btc_sell_price)
    tester.listen_reports(3)
    
    # Test 3: Cancel the limit order
    print("Test 3: Cancel Order")
    print("-" * 40)
    tester.cancel_order(cl_id2)
    tester.listen_reports(3)
    
    # Test 4: Another small limit order
    print("Test 4: Small Limit Sell")
    print("-" * 40)
    cl_id3 = tester.send_order("ETH", "sell", 0.001, "limit", price=eth_sell_price)
    tester.listen_reports(3)
    
    print("="*70)
    print("Tests Complete")
    print("="*70)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n⚠ Interrupted by user")
        sys.exit(0)
