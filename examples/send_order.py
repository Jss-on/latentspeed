import zmq
import json
import time

# Setup ZeroMQ client
context = zmq.Context()
socket = context.socket(zmq.PUSH)
socket.connect("tcp://127.0.0.1:5601")

# Create order
order = {
    "version": 1,
    "cl_id": "order_123_456",  # Unique client order ID
    "action": "place",         # "place", "cancel", "replace"
    "venue_type": "cex",       # Only "cex" supported currently
    "venue": "bybit",          # Exchange name
    "product_type": "perpetual",    # "spot" or "perpetual"
    "details": {
        "symbol": "ETHUSDT",
        "side": "buy",         # "buy" or "sell"
        "order_type": "limit", # "limit" or "market"
        "size": "2",
        "price": "4361.0",
        "time_in_force": "GTC"  # Optional
    },
    "ts_ns": int(time.time() * 1_000_000_000),
    "tags": {
        "strategy": "my_strategy",
        "session": "demo"
    }
}

# Send order
socket.send_string(json.dumps(order))