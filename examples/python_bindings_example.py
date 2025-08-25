#!/usr/bin/env python3
"""
Latentspeed Python Bindings Example
Demonstrates direct C++ integration and market data processing
"""

import latentspeed
import time
import threading

def demo_trading_engine():
    """Demonstrate basic trading engine control"""
    print("=== Trading Engine Demo ===")
    
    # Create trading engine instance
    engine = latentspeed.TradingEngineService()
    
    # Initialize the engine
    if engine.initialize():
        print("✓ Trading engine initialized successfully")
        
        # Start the engine
        engine.start()
        print(f"✓ Trading engine started (running: {engine.is_running()})")
        
        # Let it run for a few seconds
        time.sleep(2)
        
        # Stop the engine
        engine.stop()
        print("✓ Trading engine stopped")
    else:
        print("✗ Failed to initialize trading engine")

def demo_market_data_structures():
    """Demonstrate market data structure usage"""
    print("\n=== Market Data Structures Demo ===")
    
    # Create and populate trade data
    trade = latentspeed.TradeData()
    trade.exchange = "BYBIT"
    trade.symbol = "BTC-USDT"
    trade.price = 50000.0
    trade.amount = 0.1
    trade.side = "buy"
    trade.timestamp_ns = int(time.time() * 1e9)
    trade.trade_id = "demo_12345"
    trade.trading_volume = trade.price * trade.amount
    
    print(f"Trade: {trade.exchange} {trade.symbol}")
    print(f"  Price: ${trade.price:,.2f}")
    print(f"  Amount: {trade.amount}")
    print(f"  Volume: ${trade.trading_volume:,.2f}")
    print(f"  Side: {trade.side}")
    
    # Create and populate orderbook data
    book = latentspeed.OrderBookData()
    book.exchange = "BYBIT"
    book.symbol = "BTC-USDT"
    book.best_bid_price = 49995.0
    book.best_bid_size = 0.5
    book.best_ask_price = 50005.0
    book.best_ask_size = 0.3
    book.timestamp_ns = int(time.time() * 1e9)
    
    # Calculate derived metrics
    book.midpoint = (book.best_bid_price + book.best_ask_price) / 2.0
    book.relative_spread = (book.best_ask_price - book.best_bid_price) / book.midpoint
    total_vol = book.best_bid_size + book.best_ask_size
    book.imbalance_lvl1 = (book.best_bid_size - book.best_ask_size) / total_vol if total_vol > 0 else 0.0
    
    print(f"\nOrderbook: {book.exchange} {book.symbol}")
    print(f"  Best Bid: ${book.best_bid_price:,.2f} @ {book.best_bid_size}")
    print(f"  Best Ask: ${book.best_ask_price:,.2f} @ {book.best_ask_size}")
    print(f"  Midpoint: ${book.midpoint:,.2f}")
    print(f"  Spread: {book.relative_spread*100:.4f}%")
    print(f"  L1 Imbalance: {book.imbalance_lvl1:.4f}")

def demo_rolling_statistics():
    """Demonstrate FastRollingStats usage"""
    print("\n=== Rolling Statistics Demo ===")
    
    # Create rolling stats with 10-period window
    stats = latentspeed.FastRollingStats(window_size=10)
    
    # Simulate trade price updates
    trade_prices = [50000, 50100, 49900, 50200, 49950, 50150, 50050, 49850, 50250, 50000]
    
    print("Processing trade prices for volatility calculation:")
    for i, price in enumerate(trade_prices):
        result = stats.update_trade(price)
        print(f"  Trade {i+1}: ${price:,} -> Volatility: {result.volatility_transaction_price:.6f} "
              f"(window: {result.transaction_price_window_size})")
    
    # Simulate orderbook updates
    print("\nProcessing orderbook updates for OFI calculation:")
    orderbook_data = [
        (50000, 50000, 0.5, 50100, 0.3),  # midpoint, bid_price, bid_size, ask_price, ask_size
        (50050, 50000, 0.6, 50100, 0.2),
        (50025, 49950, 0.4, 50100, 0.4),
        (50075, 50050, 0.7, 50100, 0.1),
    ]
    
    for i, (mid, bid_p, bid_s, ask_p, ask_s) in enumerate(orderbook_data):
        book_result = stats.update_book(mid, bid_p, bid_s, ask_p, ask_s)
        print(f"  Book {i+1}: Mid=${mid:,} -> Vol: {book_result.volatility_mid:.6f}, "
              f"OFI: {book_result.ofi_rolling:.6f}")

def demo_order_creation():
    """Demonstrate order structure creation"""
    print("\n=== Order Creation Demo ===")
    
    # Create execution order
    order = latentspeed.ExecutionOrder()
    order.version = 1
    order.cl_id = f"demo_order_{int(time.time())}"
    order.action = "place"
    order.venue_type = "cex"
    order.venue = "bybit"
    order.product_type = "spot"
    order.ts_ns = int(time.time() * 1e9)
    
    # Set order details
    order.details["symbol"] = "BTC-USDT"
    order.details["side"] = "buy"
    order.details["order_type"] = "limit"
    order.details["size"] = "0.1"
    order.details["price"] = "50000.0"
    order.details["time_in_force"] = "gtc"
    
    # Set tags
    order.tags["strategy"] = "demo_strategy"
    order.tags["session"] = "python_bindings_test"
    order.tags["risk_level"] = "low"
    
    print(f"Created ExecutionOrder:")
    print(f"  Client ID: {order.cl_id}")
    print(f"  Action: {order.action}")
    print(f"  Venue: {order.venue_type}/{order.venue}")
    print(f"  Product: {order.product_type}")
    print(f"  Details: {dict(order.details)}")
    print(f"  Tags: {dict(order.tags)}")
    
    # Create execution report (what the engine would generate)
    report = latentspeed.ExecutionReport()
    report.version = 1
    report.cl_id = order.cl_id
    report.status = "accepted"
    report.reason_code = "ok"
    report.reason_text = "Order accepted for execution"
    report.ts_ns = int(time.time() * 1e9)
    report.tags = order.tags  # Copy tags from order
    
    print(f"\nGenerated ExecutionReport:")
    print(f"  Client ID: {report.cl_id}")
    print(f"  Status: {report.status}")
    print(f"  Reason: {report.reason_text}")
    
    # Create fill report
    fill = latentspeed.Fill()
    fill.version = 1
    fill.cl_id = order.cl_id
    fill.exec_id = f"exec_{int(time.time())}"
    fill.symbol_or_pair = "BTC-USDT"
    fill.price = 50005.0  # Slight slippage
    fill.size = 0.1
    fill.fee_currency = "USDT"
    fill.fee_amount = 5.0005  # 0.01% fee
    fill.ts_ns = int(time.time() * 1e9)
    
    print(f"\nGenerated Fill:")
    print(f"  Exec ID: {fill.exec_id}")
    print(f"  Symbol: {fill.symbol_or_pair}")
    print(f"  Filled: {fill.size} @ ${fill.price:,.2f}")
    print(f"  Fee: {fill.fee_amount} {fill.fee_currency}")

def main():
    """Run all demos"""
    print("Latentspeed Python Bindings Demo")
    print("=" * 50)
    
    try:
        demo_market_data_structures()
        demo_rolling_statistics()
        demo_order_creation()
        
        # Note: Trading engine demo requires proper build and dependencies
        print("\n" + "=" * 50)
        print("To test the trading engine itself, ensure:")
        print("1. Build completed successfully with Python bindings")
        print("2. All dependencies (ZMQ, CCAPI, etc.) are available")
        print("3. Run from the build directory or install the module")
        print("\nUncomment the line below to test the actual engine:")
        print("# demo_trading_engine()")
        
    except Exception as e:
        print(f"Error running demo: {e}")
        print("\nTrouble shooting:")
        print("1. Ensure you've built the project with Python bindings enabled")
        print("2. Install or add the module to PYTHONPATH")
        print("3. Check that all system dependencies are installed")

if __name__ == "__main__":
    main()
