#!/usr/bin/env python3
"""
Live Lead-Lag Trading Strategy
Integrates marketstream (ZMQ) → Strategy Logic → Trading Engine (ZMQ)

Based on arkpad-ahab2 lead-lag strategy:
- Monitors BTC (leader) and ETH (follower) price movements
- Detects significant jumps in BTC price
- Calculates correlation between BTC and ETH
- Trades ETH when strong lead-lag relationship detected
"""

import zmq
import json
import time
import numpy as np
import uuid
import signal
import sys
from collections import deque
from datetime import datetime
from typing import Dict, List, Optional, Tuple


class LeadLagStrategy:
    """Lead-lag trading strategy with live execution"""
    
    def __init__(self, config: Dict):
        self.config = config
        
        # ZMQ setup for market data (subscribe to marketstream)
        self.zmq_context = zmq.Context()
        
        # Subscribe to trades stream (port 5556)
        self.trades_socket = self.zmq_context.socket(zmq.SUB)
        self.trades_socket.connect("tcp://127.0.0.1:5556")
        self.trades_socket.setsockopt_string(zmq.SUBSCRIBE, "")
        self.trades_socket.setsockopt(zmq.RCVTIMEO, 100)  # 100ms timeout
        
        # Subscribe to orderbook stream (port 5557) 
        self.books_socket = self.zmq_context.socket(zmq.SUB)
        self.books_socket.connect("tcp://127.0.0.1:5557")
        self.books_socket.setsockopt_string(zmq.SUBSCRIBE, "")
        self.books_socket.setsockopt(zmq.RCVTIMEO, 100)
        
        # ZMQ setup for order execution (send to trading engine)
        self.order_socket = self.zmq_context.socket(zmq.PUSH)
        self.order_socket.connect("tcp://127.0.0.1:5601")
        
        # Subscribe to execution reports
        self.report_socket = self.zmq_context.socket(zmq.SUB)
        self.report_socket.connect("tcp://127.0.0.1:5602")
        self.report_socket.setsockopt(zmq.SUBSCRIBE, b"exec.report")
        self.report_socket.setsockopt(zmq.SUBSCRIBE, b"exec.fill")
        self.report_socket.setsockopt(zmq.RCVTIMEO, 100)
        
        # Strategy parameters
        self.leader_symbol = config.get("leader_symbol", "BTC")
        self.follower_symbol = config.get("follower_symbol", "ETH")
        self.jump_threshold_bps = config.get("jump_threshold_bps", 30.0)
        self.min_correlation = config.get("min_correlation", 0.60)
        self.lookback_window = config.get("lookback_window", 100)
        self.base_position_usd = config.get("base_position_usd", 2.0)
        self.max_position_usd = config.get("max_position_usd", 4.0)
        self.stop_loss_bps = config.get("stop_loss_bps", 50.0)
        self.take_profit_bps = config.get("take_profit_bps", 100.0)
        self.max_open_positions = config.get("max_open_positions", 2)
        self.position_timeout_seconds = config.get("position_timeout_seconds", 60)
        
        # Price history buffers
        self.leader_prices = deque(maxlen=self.lookback_window + 50)
        self.follower_prices = deque(maxlen=self.lookback_window + 50)
        
        # Position tracking
        self.positions = {}
        self.pending_orders = {}
        
        # Statistics
        self.trades_executed = 0
        self.trades_won = 0
        self.trades_lost = 0
        self.total_pnl = 0.0
        
        # Running flag
        self.running = True
        
        print(f"✓ Lead-Lag Strategy initialized")
        print(f"  Leader: {self.leader_symbol} | Follower: {self.follower_symbol}")
        print(f"  Jump threshold: {self.jump_threshold_bps}bps")
        print(f"  Min correlation: {self.min_correlation}")
        print(f"  Position size: ${self.base_position_usd} - ${self.max_position_usd}")
        
    def run(self):
        """Main strategy loop"""
        print("\n" + "="*70)
        print("Lead-Lag Strategy Running - Press Ctrl+C to stop")
        print("="*70 + "\n")
        
        last_stats_time = time.time()
        stats_interval = 30  # Print stats every 30 seconds
        
        try:
            while self.running:
                # Process market data
                self._process_trades()
                self._process_orderbooks()
                
                # Process execution reports
                self._process_execution_reports()
                
                # Manage existing positions
                self._manage_positions()
                
                # Print stats periodically
                if time.time() - last_stats_time >= stats_interval:
                    self._print_stats()
                    last_stats_time = time.time()
                
                time.sleep(0.001)  # Small sleep to prevent CPU spinning
                
        except KeyboardInterrupt:
            print("\n⚠ Strategy interrupted by user")
        finally:
            self._cleanup()
    
    def _process_trades(self):
        """Process incoming trade data from marketstream"""
        try:
            topic = self.trades_socket.recv_string(flags=zmq.NOBLOCK)
            message = self.trades_socket.recv_string(flags=zmq.NOBLOCK)
            trade = json.loads(message)
            symbol = trade.get("symbol", "")
            
            # Normalize symbol (remove -USDT-PERP suffix for Hyperliquid)
            normalized_symbol = symbol.replace("-USDT-PERP", "").replace("USDT", "")
            
            price_point = {
                "timestamp_ns": trade.get("timestamp_ns", int(time.time() * 1e9)),
                "price": float(trade.get("price", 0)),
                "volume": float(trade.get("amount", 0))
            }
            
            if normalized_symbol == self.leader_symbol:
                self.leader_prices.append(price_point)
                
                # Check for jump in leader
                jump_detected, jump_bps = self._detect_jump(self.leader_prices)
                if jump_detected:
                    print(f"🔔 Jump detected in {self.leader_symbol}: {jump_bps:.2f}bps")
                    self._evaluate_trade_signal(jump_bps)
                    
            elif normalized_symbol == self.follower_symbol:
                self.follower_prices.append(price_point)
                
        except zmq.Again:
            pass  # No message available
        except Exception as e:
            print(f"⚠ Error processing trade: {e}")
    
    def _process_orderbooks(self):
        """Process incoming orderbook data"""
        try:
            topic = self.books_socket.recv_string(flags=zmq.NOBLOCK)
            message = self.books_socket.recv_string(flags=zmq.NOBLOCK)
            book = json.loads(message)
            # Can use orderbook data for better price tracking if needed
        except zmq.Again:
            pass
        except Exception as e:
            print(f"⚠ Error processing orderbook: {e}")
    
    def _process_execution_reports(self):
        """Process execution reports from trading engine"""
        try:
            topic = self.report_socket.recv_string(flags=zmq.NOBLOCK)
            message = self.report_socket.recv_string(flags=zmq.NOBLOCK)
            
            if topic == "exec.report":
                report = json.loads(message)
                cl_id = report.get("cl_id")
                status = report.get("status")
                print(f"📊 Order {cl_id}: {status}")
                
                if cl_id in self.pending_orders:
                    del self.pending_orders[cl_id]
                    
            elif topic == "exec.fill":
                fill = json.loads(message)
                cl_id = fill.get("cl_id")
                print(f"💰 Fill {cl_id}: {fill.get('size')} @ {fill.get('price')}")
                
        except zmq.Again:
            pass
        except Exception as e:
            print(f"⚠ Error processing execution report: {e}")
    
    def _detect_jump(self, prices: deque) -> Tuple[bool, float]:
        """Detect significant price jump"""
        if len(prices) < 2:
            return False, 0.0
        
        current_price = prices[-1]["price"]
        prev_price = prices[-2]["price"]
        
        if prev_price == 0:
            return False, 0.0
        
        change_bps = abs((current_price / prev_price - 1.0) * 10000.0)
        is_jump = change_bps >= self.jump_threshold_bps
        
        return is_jump, change_bps
    
    def _calculate_correlation(self) -> float:
        """Calculate Pearson correlation between leader and follower returns"""
        min_size = min(len(self.leader_prices), len(self.follower_prices))
        
        if min_size < 10:
            return 0.0
        
        window = min(min_size, self.lookback_window)
        
        # Calculate log returns
        leader_returns = []
        follower_returns = []
        
        leader_list = list(self.leader_prices)
        follower_list = list(self.follower_prices)
        
        for i in range(len(leader_list) - window, len(leader_list) - 1):
            if leader_list[i]["price"] > 0 and leader_list[i+1]["price"] > 0:
                ret = np.log(leader_list[i+1]["price"] / leader_list[i]["price"])
                leader_returns.append(ret)
        
        for i in range(len(follower_list) - window, len(follower_list) - 1):
            if follower_list[i]["price"] > 0 and follower_list[i+1]["price"] > 0:
                ret = np.log(follower_list[i+1]["price"] / follower_list[i]["price"])
                follower_returns.append(ret)
        
        if len(leader_returns) < 10 or len(follower_returns) < 10:
            return 0.0
        
        # Align arrays
        min_len = min(len(leader_returns), len(follower_returns))
        leader_returns = np.array(leader_returns[-min_len:])
        follower_returns = np.array(follower_returns[-min_len:])
        
        # Calculate Pearson correlation
        if len(leader_returns) == 0:
            return 0.0
        
        correlation = np.corrcoef(leader_returns, follower_returns)[0, 1]
        
        return correlation if not np.isnan(correlation) else 0.0
    
    def _evaluate_trade_signal(self, jump_bps: float):
        """Evaluate whether to trade based on jump and correlation"""
        # Check position limits
        if len(self.positions) >= self.max_open_positions:
            print(f"  ⏸ Max positions reached ({self.max_open_positions})")
            return
        
        # Calculate correlation
        correlation = self._calculate_correlation()
        print(f"  📈 Correlation: {correlation:.4f}")
        
        if abs(correlation) < self.min_correlation:
            print(f"  ⏸ Correlation too low: {abs(correlation):.4f} < {self.min_correlation}")
            return
        
        # Determine trade direction
        if len(self.leader_prices) < 2:
            return
        
        current = self.leader_prices[-1]["price"]
        prev = self.leader_prices[-2]["price"]
        leader_direction = 1 if current > prev else -1
        
        # If correlation is negative, invert direction
        trade_direction = leader_direction if correlation > 0 else -leader_direction
        side = "buy" if trade_direction > 0 else "sell"
        
        # Calculate position size
        signal_strength = abs(correlation) * (jump_bps / 100.0)
        size_multiplier = min(signal_strength, 2.0)
        position_usd = min(self.base_position_usd * size_multiplier, self.max_position_usd)
        
        print(f"  🎯 Signal: {side.upper()} {self.follower_symbol}")
        print(f"     Strength: {signal_strength:.2f} | Size: ${position_usd:.2f}")
        
        # Execute trade
        self._execute_trade(side, position_usd)
    
    def _execute_trade(self, side: str, position_usd: float):
        """Execute a trade order"""
        if len(self.follower_prices) == 0:
            print("  ⚠ No price data for follower")
            return
        
        current_price = self.follower_prices[-1]["price"]
        if current_price <= 0:
            print(f"  ⚠ Invalid price: {current_price}")
            return
        
        # Calculate size (conservative - use slightly less for fees/slippage)
        size = (position_usd * 0.98) / current_price
        size = round(size, 4)  # Round to 4 decimals
        
        if size <= 0:
            print(f"  ⚠ Size too small: {size}")
            return
        
        # Generate client order ID
        cl_id = f"leadlag_{int(time.time() * 1000)}"
        
        # Calculate stop loss and take profit
        if side == "buy":
            stop_loss = current_price * (1.0 - self.stop_loss_bps / 10000.0)
            take_profit = current_price * (1.0 + self.take_profit_bps / 10000.0)
        else:
            stop_loss = current_price * (1.0 + self.stop_loss_bps / 10000.0)
            take_profit = current_price * (1.0 - self.take_profit_bps / 10000.0)
        
        # Create order
        order = {
            "version": 1,
            "cl_id": cl_id,
            "action": "place",
            "venue_type": "cex",
            "venue": "hyperliquid",
            "product_type": "perpetual",
            "details": {
                "symbol": self.follower_symbol,
                "side": side.lower(),
                "order_type": "market",
                "size": str(size),
                "reduce_only": "false"
            },
            "ts_ns": int(time.time() * 1_000_000_000),
            "tags": {
                "strategy": "lead_lag",
                "entry_price": str(current_price)
            }
        }
        
        # Track position
        self.positions[cl_id] = {
            "symbol": self.follower_symbol,
            "side": side,
            "entry_price": current_price,
            "size": size,
            "entry_time": time.time(),
            "stop_loss": stop_loss,
            "take_profit": take_profit
        }
        
        # Send order
        try:
            self.order_socket.send_string(json.dumps(order))
            self.pending_orders[cl_id] = order
            self.trades_executed += 1
            
            print(f"  ✅ Order sent: {side.upper()} {size} {self.follower_symbol} @ ${current_price:.2f}")
            print(f"     SL: ${stop_loss:.2f} | TP: ${take_profit:.2f}")
            
        except Exception as e:
            print(f"  ❌ Failed to send order: {e}")
            del self.positions[cl_id]
    
    def _manage_positions(self):
        """Check and manage existing positions"""
        if len(self.follower_prices) == 0:
            return
        
        current_price = self.follower_prices[-1]["price"]
        current_time = time.time()
        
        to_close = []
        
        for cl_id, pos in self.positions.items():
            should_close = False
            reason = None
            
            # Check stop loss
            if pos["side"] == "buy" and current_price <= pos["stop_loss"]:
                should_close = True
                reason = "stop_loss"
                self.trades_lost += 1
            elif pos["side"] == "sell" and current_price >= pos["stop_loss"]:
                should_close = True
                reason = "stop_loss"
                self.trades_lost += 1
            
            # Check take profit
            if pos["side"] == "buy" and current_price >= pos["take_profit"]:
                should_close = True
                reason = "take_profit"
                self.trades_won += 1
            elif pos["side"] == "sell" and current_price <= pos["take_profit"]:
                should_close = True
                reason = "take_profit"
                self.trades_won += 1
            
            # Check timeout
            age = current_time - pos["entry_time"]
            if age > self.position_timeout_seconds:
                should_close = True
                reason = "timeout"
            
            if should_close:
                # Calculate PnL
                if pos["side"] == "buy":
                    pnl = (current_price - pos["entry_price"]) * pos["size"]
                else:
                    pnl = (pos["entry_price"] - current_price) * pos["size"]
                
                self.total_pnl += pnl
                
                print(f"\n💼 Closing position {cl_id}")
                print(f"   Reason: {reason} | PnL: ${pnl:.2f}")
                
                # Send close order
                close_side = "sell" if pos["side"] == "buy" else "buy"
                close_order = {
                    "version": 1,
                    "cl_id": f"{cl_id}_close",
                    "action": "place",
                    "venue_type": "cex",
                    "venue": "hyperliquid",
                    "product_type": "perpetual",
                    "details": {
                        "symbol": pos["symbol"],
                        "side": close_side,
                        "order_type": "market",
                        "size": str(pos["size"]),
                        "reduce_only": "true"
                    },
                    "ts_ns": int(time.time() * 1_000_000_000),
                    "tags": {
                        "strategy": "lead_lag",
                        "reason": reason,
                        "pnl": str(pnl)
                    }
                }
                
                try:
                    self.order_socket.send_string(json.dumps(close_order))
                    to_close.append(cl_id)
                except Exception as e:
                    print(f"   ❌ Failed to close position: {e}")
        
        # Remove closed positions
        for cl_id in to_close:
            del self.positions[cl_id]
    
    def _print_stats(self):
        """Print strategy statistics"""
        print("\n" + "="*70)
        print(f"Strategy Stats @ {datetime.now().strftime('%H:%M:%S')}")
        print("="*70)
        print(f"Leader data points: {len(self.leader_prices)}")
        print(f"Follower data points: {len(self.follower_prices)}")
        print(f"Open positions: {len(self.positions)}")
        print(f"Pending orders: {len(self.pending_orders)}")
        print(f"Trades executed: {self.trades_executed}")
        print(f"Won: {self.trades_won} | Lost: {self.trades_lost}")
        print(f"Total PnL: ${self.total_pnl:.2f}")
        print("="*70 + "\n")
    
    def _cleanup(self):
        """Cleanup resources"""
        print("\n🛑 Shutting down strategy...")
        self._print_stats()
        
        # Close positions if any
        if self.positions:
            print(f"⚠ Warning: {len(self.positions)} open positions remaining")
        
        # Close sockets
        self.trades_socket.close()
        self.books_socket.close()
        self.order_socket.close()
        self.report_socket.close()
        self.zmq_context.term()
        
        print("✓ Strategy stopped")


def main():
    """Main entry point"""
    
    # Strategy configuration
    config = {
        "leader_symbol": "BTC",
        "follower_symbol": "ETH",
        "jump_threshold_bps": 25.0,        # 0.25% jump to trigger
        "min_correlation": 0.55,           # Minimum correlation to trade
        "lookback_window": 100,            # Price history window
        "base_position_usd": 2.0,        # Base position size
        "max_position_usd": 4.0,         # Max position size
        "stop_loss_bps": 40.0,             # 0.4% stop loss
        "take_profit_bps": 80.0,           # 0.8% take profit
        "max_open_positions": 2,           # Max concurrent positions
        "position_timeout_seconds": 120    # Max hold time (2 minutes)
    }
    
    # Create and run strategy
    strategy = LeadLagStrategy(config)
    
    # Handle Ctrl+C gracefully
    def signal_handler(sig, frame):
        strategy.running = False
    
    signal.signal(signal.SIGINT, signal_handler)
    
    # Run strategy
    strategy.run()


if __name__ == "__main__":
    main()
