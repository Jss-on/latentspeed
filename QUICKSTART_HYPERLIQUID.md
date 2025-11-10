# Quickstart: Hyperliquid Connector

**⚡ Get started in 5 minutes!**

---

## 🚀 Build & Run

```bash
# 1. Build the project
cd /home/tensor/latentspeed
./run.sh --release

# 2. Set your credentials
export LATENTSPEED_HYPERLIQUID_USER_ADDRESS="0xYourAddress"
export LATENTSPEED_HYPERLIQUID_PRIVATE_KEY="0xYourPrivateKey"

# 3. Start the engine (testnet)
./build/trading_engine_service --exchange hyperliquid --demo

# 4. (Optional) For mainnet
# ./build/trading_engine_service --exchange hyperliquid --live-trade
```

---

## 📝 Quick Test

### Python Client (Order Placement)

```python
import zmq
import json
import time

# Connect to engine
context = zmq.Context()
order_socket = context.socket(zmq.PUSH)
order_socket.connect("tcp://127.0.0.1:5601")

report_socket = context.socket(zmq.SUB)
report_socket.connect("tcp://127.0.0.1:5602")
report_socket.setsockopt_string(zmq.SUBSCRIBE, "")

# Send a limit order (far from market - won't fill immediately)
order = {
    "action": "place",
    "version": 1,
    "cl_id": "TEST-001",
    "venue": "hyperliquid",
    "venue_type": "cex",
    "product_type": "perpetual",
    "symbol": "BTC-USD",
    "side": "buy",
    "order_type": "limit",
    "size": 0.001,  # Minimum size
    "price": 30000.0,  # Far from market
    "reduce_only": False
}

print("Sending order...")
order_socket.send_json(order)

# Wait for report
print("Waiting for execution report...")
report = report_socket.recv_json(flags=zmq.NOBLOCK)
print(f"Report: {json.dumps(report, indent=2)}")
```

---

## ✅ What You Should See

### In Engine Logs:
```
[HFT-Engine] Exchange adapter initialized: hyperliquid (Hummingbot pattern)
[HyperliquidAdapter] Initializing Hummingbot-pattern connector...
[HyperliquidAdapter] Connected successfully
[HyperliquidConnector] Order placed: TEST-001 in 342ns
[HyperliquidConnector] State transition: PENDING_SUBMIT -> OPEN
```

### In Python Client:
```json
{
  "cl_id": "TEST-001",
  "status": "accepted",
  "exchange_order_id": "123456789",
  "venue": "hyperliquid",
  "symbol": "BTC-USD"
}
```

---

## 🐛 Troubleshooting

### "Not connected" Error
```bash
# Check credentials
echo $LATENTSPEED_HYPERLIQUID_USER_ADDRESS
echo $LATENTSPEED_HYPERLIQUID_PRIVATE_KEY

# Verify network
ping api.hyperliquid.xyz

# Check logs
tail -f logs/trading_engine.log
```

### Build Errors
```bash
# Clean build
rm -rf build/
./run.sh --release

# Check dependencies
vcpkg list | grep -E "(boost|openssl|nlohmann|spdlog)"
```

---

## 📚 More Info

- **Full Guide**: [docs/HYPERLIQUID_CONNECTOR_IMPLEMENTATION_COMPLETE.md](docs/HYPERLIQUID_CONNECTOR_IMPLEMENTATION_COMPLETE.md)
- **Integration Plan**: [docs/HYPERLIQUID_CONNECTOR_INTEGRATION_PLAN.md](docs/HYPERLIQUID_CONNECTOR_INTEGRATION_PLAN.md)
- **Architecture**: [docs/refactoring/00_OVERVIEW.md](docs/refactoring/00_OVERVIEW.md)

---

**Ready to trade!** 🎯
