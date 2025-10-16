# LatentSpeed Production Deployment

## Architecture Overview

LatentSpeed uses a **two-executable architecture** for production:

```
┌─────────────────────────────────────────────────────────────────┐
│                    PRODUCTION ARCHITECTURE                      │
└─────────────────────────────────────────────────────────────────┘

    Exchange APIs                                    Trading Decisions
         │                                                  │
         ▼                                                  ▼
  ┌──────────────┐      ZMQ (5556/5557)        ┌────────────────────┐
  │ marketstream │ ────────────────────────────▶│ trading_engine_    │
  │              │   Preprocessed Market Data   │ server             │
  └──────────────┘                              └────────────────────┘
   • WebSocket                                   • Strategy Engine
   • Data Parsing                                • Risk Management
   • Feature Calc                                • Order Execution
   • ZMQ Publish                                 • Position Tracking
```

## Two Production Executables

### 1. **marketstream** - Market Data Provider
**Location:** `src/marketstream.cpp`  
**Purpose:** Connect to exchanges, preprocess data, publish via ZMQ

**Responsibilities:**
- WebSocket connections to multiple exchanges (dYdX, Bybit, Binance, etc.)
- Parse raw market data (trades, orderbook snapshots)
- Calculate market microstructure features:
  - Midpoint, spread, imbalance
  - Depth, breadth, OFI
  - Rolling volatility
- Publish preprocessed data via ZMQ (ports 5556/5557)

**Launch:**
```bash
./build/marketstream config.yml
```

**Config:** `config.yml`
```yaml
zmq:
  port: 5556  # trades, books on 5557
  window_size: 20

feeds:
  - exchange: dydx
    symbols: [BTC-USD, ETH-USD]
```

---

### 2. **trading_engine_server** - Trading Engine
**Location:** `src/main.cpp`  
**Purpose:** Consume market data, execute strategies, manage risk, place orders

**Responsibilities:**
- Subscribe to ZMQ feeds (trades and orderbooks)
- Run trading strategies with market data
- Risk management and position tracking
- Order placement and execution
- Account state management

**Launch:**
```bash
./build/trading_engine_server --config trading_config.yml
```

---

## Key Differences from Python Marketstream

| Feature | Python Marketstream | C++ MarketStream (ours) |
|---------|-------------------|------------------------|
| **Language** | Python (CryptoFeed) | C++ (Native WebSocket) |
| **Performance** | ~100-500μs latency | ~10-50μs latency (~10x faster) |
| **Storage** | Redis Streams → TimescaleDB | Direct ZMQ only |
| **Use Case** | Data collection + streaming | Real-time trading only |
| **Dependencies** | CryptoFeed, Redis, PostgreSQL | OpenSSL, Boost.Beast, ZMQ |

### Why No Redis?
- **Ultra-low latency**: Direct ZMQ eliminates Redis round-trip (~50-100μs saved)
- **Simpler architecture**: Fewer moving parts in production
- **Trading focus**: We don't need historical storage in the critical path
- **Data format**: Same preprocessing features as Python version

---

## Data Format (ZMQ Messages)

Both Python and C++ marketstream produce **identical** preprocessed data format:

### Trade Message (Port 5556)
```json
{
  "exchange": "DYDX",
  "symbol": "BTC-USD",
  "timestamp_ns": 1640995200123456789,
  "receipt_timestamp_ns": 1640995200123456890,
  "price": 50000.50,
  "amount": 0.5,
  "side": "buy",
  "seq": 12345,
  "transaction_price": 50000.50,
  "trading_volume": 25000.25,
  "volatility": 0.0023
}
```

### Orderbook Message (Port 5557)
```json
{
  "exchange": "DYDX",
  "symbol": "BTC-USD",
  "timestamp_ns": 1640995200123456789,
  "receipt_timestamp_ns": 1640995200123456890,
  "seq": 12346,
  "bids": [[50000.0, 1.5], [49999.5, 2.0], ...],
  "asks": [[50000.5, 1.2], [50001.0, 1.8], ...],
  "midpoint": 50000.25,
  "relative_spread": 0.00001,
  "breadth": 150001.75,
  "imbalance_lvl1": 0.2,
  "bid_depth_n": 250000.50,
  "ask_depth_n": 240000.30,
  "volatility_mid": 0.0018,
  "ofi_rolling": 0.15
}
```

---

## Production Deployment

### Build
```bash
./run.sh --release
```

This creates:
- `./build/marketstream` - Market data provider
- `./build/trading_engine_server` - Trading engine

### Run Production Stack

**Terminal 1 - MarketStream:**
```bash
cd /home/tensor/latentspeed
./build/marketstream config.yml
```

**Terminal 2 - Trading Engine:**
```bash
cd /home/tensor/latentspeed
./build/trading_engine_server --config trading_config.yml
```

### Systemd Service (Recommended)

Create `/etc/systemd/system/latentspeed-marketstream.service`:
```ini
[Unit]
Description=LatentSpeed MarketStream
After=network.target

[Service]
Type=simple
User=tensor
WorkingDirectory=/home/tensor/latentspeed
ExecStart=/home/tensor/latentspeed/build/marketstream config.yml
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Create `/etc/systemd/system/latentspeed-trading.service`:
```ini
[Unit]
Description=LatentSpeed Trading Engine
After=network.target latentspeed-marketstream.service
Requires=latentspeed-marketstream.service

[Service]
Type=simple
User=tensor
WorkingDirectory=/home/tensor/latentspeed
ExecStart=/home/tensor/latentspeed/build/trading_engine_server --config trading_config.yml
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start:
```bash
sudo systemctl daemon-reload
sudo systemctl enable latentspeed-marketstream
sudo systemctl enable latentspeed-trading
sudo systemctl start latentspeed-marketstream
sudo systemctl start latentspeed-trading
```

### Monitoring
```bash
# Check status
sudo systemctl status latentspeed-marketstream
sudo systemctl status latentspeed-trading

# View logs
journalctl -u latentspeed-marketstream -f
journalctl -u latentspeed-trading -f

# Or application logs
tail -f marketstream.log
tail -f trading_engine.log
```

---

## Configuration Management

### Environment-Specific Configs

```bash
config/
├── config.yml              # Default/development
├── production.yml          # Production settings
├── staging.yml             # Staging environment
└── test.yml               # Testing/simulation
```

**Launch with specific config:**
```bash
./build/marketstream config/production.yml
```

### Dynamic Symbol Updates

Modify `config.yml` and restart:
```bash
sudo systemctl restart latentspeed-marketstream
```

No recompilation needed! ✅

---

## Performance Tuning

### CPU Affinity (HFT Mode)
```bash
# Pin marketstream to cores 0-3
taskset -c 0-3 ./build/marketstream config.yml

# Pin trading engine to cores 4-7
taskset -c 4-7 ./build/trading_engine_server --config trading_config.yml
```

### Network Optimization
```bash
# Increase socket buffers
sudo sysctl -w net.core.rmem_max=134217728
sudo sysctl -w net.core.wmem_max=134217728

# Reduce TCP latency
sudo sysctl -w net.ipv4.tcp_low_latency=1
```

### Real-time Priority
```bash
sudo chrt -f 80 ./build/marketstream config.yml
sudo chrt -f 80 ./build/trading_engine_server --config trading_config.yml
```

---

## Troubleshooting

### marketstream not connecting
```bash
# Check WebSocket connectivity
curl -I https://indexer.dydx.trade/v4/ws

# Verify ZMQ ports are free
netstat -tuln | grep 5556
netstat -tuln | grep 5557
```

### trading_engine_server not receiving data
```bash
# Test ZMQ connection
python3 -c "
import zmq
ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect('tcp://127.0.0.1:5556')
sock.subscribe(b'')
print('Listening for trades...')
print(sock.recv_string())
"
```

### High CPU usage
- Reduce `window_size` in config (default: 20)
- Increase `snapshot_interval` for orderbooks
- Disable debug logging in production

---

## Future Enhancements (Not Implemented Yet)

- [ ] **Redis Streams**: Optional persistence layer
- [ ] **Prometheus Metrics**: Real-time monitoring
- [ ] **WebSocket Reconnection**: Automatic retry with exponential backoff
- [ ] **Multi-region Failover**: Geographic redundancy
- [ ] **Backtesting Mode**: Replay historical data

---

## Production Checklist

Before deploying to production:

- [ ] Build in Release mode (`./run.sh --release`)
- [ ] Test all exchange connections
- [ ] Verify ZMQ data flow (trades + orderbooks)
- [ ] Configure proper log levels (info/warn)
- [ ] Set up systemd services
- [ ] Enable automatic restart on failure
- [ ] Configure CPU affinity for HFT
- [ ] Set up monitoring and alerts
- [ ] Document symbol changes process
- [ ] Test graceful shutdown (Ctrl+C)

---

## Support

For issues or questions:
- Check logs: `marketstream.log`, `trading_engine.log`
- Review system logs: `journalctl -u latentspeed-*`
- Enable debug logging temporarily: set `level: debug` in config.yml
