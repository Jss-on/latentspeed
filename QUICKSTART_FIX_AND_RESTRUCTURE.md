# Quick Start: Fix Headers & Restructure

This guide gets you from current state to production-ready architecture in 3 phases.

---

## 🎯 What We're Fixing

**Problem 1:** 110 function implementations in headers (should be in .cpp)  
**Problem 2:** Exchange-specific code mixed with core connector code  
**Solution:** Separate implementations + Hummingbot-style directory structure

---

## ✅ Already Done (2 files)

1. ✅ `hyperliquid_integrated_connector.h` → `.cpp` (250 lines moved)
2. ✅ `hyperliquid_user_stream_data_source.h` → `.cpp` (270 lines moved)

**Result:** ~520 lines moved to .cpp, 2 headers cleaned

---

## 🚀 Phase 1: Complete Critical Files (30 minutes)

### Step 1.1: Fix `hyperliquid_perpetual_connector.h` (BIGGEST WIN)

This is the largest file (757 lines). Template already created.

```bash
# Open both files side-by-side
code include/connector/hyperliquid_perpetual_connector.h
code src/connector/hyperliquid_perpetual_connector.cpp
```

**Process:**
1. Find each method with `{ ... }` body in the header
2. **Cut** the implementation (keep just the declaration + `;`)
3. **Paste** into .cpp file, add `HyperliquidPerpetualConnector::` prefix
4. Repeat for all 20 methods

**Example:**
```cpp
// BEFORE (in .h):
std::string buy(const OrderParams& params) {
    return place_order(params, TradeType::BUY);
}

// AFTER (in .h):
std::string buy(const OrderParams& params);

// AFTER (in .cpp):
std::string HyperliquidPerpetualConnector::buy(const OrderParams& params) {
    return place_order(params, TradeType::BUY);
}
```

### Step 1.2: Fix `zmq_order_event_publisher.h` (9 methods)

Same process. Template at `src/connector/zmq_order_event_publisher.cpp`.

### Step 1.3: Fix `client_order_tracker.h` (7 methods)

Same process. Template at `src/connector/client_order_tracker.cpp`.

**After Phase 1:**
- 5 critical files completed (46 methods moved)
- ~800-1000 lines moved to .cpp
- Major compilation speedup

---

## 🏗️ Phase 2: Restructure to Hummingbot Pattern (10 minutes)

### Step 2.1: Preview Changes

```bash
cd /path/to/latentspeed
python3 tools/restructure_to_hummingbot.py
```

This shows what will be moved (dry run, no changes).

### Step 2.2: Execute Restructure

```bash
python3 tools/restructure_to_hummingbot.py --execute
```

**This will:**
- Create `include/connector/exchange/hyperliquid/`
- Create `src/connector/exchange/hyperliquid/`
- Move all Hyperliquid-specific files
- Update all #include paths
- Rename files for consistency:
  - `hyperliquid_perpetual_connector.h` → `hyperliquid_exchange.h`
  - `hyperliquid_user_stream_data_source.h` → `hyperliquid_api_user_stream_data_source.h`

### Step 2.3: Update CMakeLists.txt

Copy the generated snippet (shown by script) into your `CMakeLists.txt`:

```cmake
# Connector Core Library
set(CONNECTOR_CORE_SOURCES
    src/connector/connector_base.cpp
    src/connector/client_order_tracker.cpp
    src/connector/in_flight_order.cpp
    src/connector/zmq_order_event_publisher.cpp
)

# Hyperliquid Exchange Implementation
set(HYPERLIQUID_CONNECTOR_SOURCES
    src/connector/exchange/hyperliquid/hyperliquid_auth.cpp
    src/connector/exchange/hyperliquid/hyperliquid_exchange.cpp
    src/connector/exchange/hyperliquid/hyperliquid_integrated_connector.cpp
    src/connector/exchange/hyperliquid/hyperliquid_api_user_stream_data_source.cpp
    src/connector/exchange/hyperliquid/hyperliquid_api_order_book_data_source.cpp
)

add_library(latentspeed_connector STATIC
    ${CONNECTOR_CORE_SOURCES}
    ${HYPERLIQUID_CONNECTOR_SOURCES}
)
```

**After Phase 2:**
- Clean directory structure
- Exchange isolation complete
- Ready to add Bybit, Binance, etc.

---

## 🧪 Phase 3: Test & Validate (5 minutes)

### Step 3.1: Test Compilation

```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

### Step 3.2: Fix Any Include Errors

If you see errors like:
```
fatal error: connector/hyperliquid_auth.h: No such file or directory
```

Update to:
```cpp
#include "connector/exchange/hyperliquid/hyperliquid_auth.h"
```

The restructure script should have caught most of these, but check:
- `examples/*.cpp`
- Any test files
- `include/adapters/` files

### Step 3.3: Run Tests (if available)

```bash
cd build
ctest
```

**After Phase 3:**
- Everything compiles
- Tests pass
- Ready for production

---

## 📋 Remaining Work (Optional, Low Priority)

After the core work is done, you can optionally complete:

### Medium Priority (7 files)
- `rolling_stats.h` (7 methods)
- `position.h` (5 methods)
- `order_book.h` (4 methods)
- `trading_rule.h` (4 methods)
- `hyperliquid_order_book_data_source.h` (10 methods)
- `hyperliquid_marketstream_adapter.h` (2 methods)
- `trading_engine_service.h` (2 methods)

### Keep Inline (OK as-is)
- `hyperliquid_nonce.h` - Atomic operations
- `venue_router.h` - Simple registry
- Any constexpr/template functions

---

## 🎯 Success Criteria

✅ **Phase 1 Complete When:**
- 5 critical headers cleaned (implementations in .cpp)
- Compilation time reduced by 30-40%
- All tests pass

✅ **Phase 2 Complete When:**
- Directory structure matches Hummingbot pattern
- All Hyperliquid files in `connector/exchange/hyperliquid/`
- Core connector files separate from exchange-specific

✅ **Phase 3 Complete When:**
- `make -j4` succeeds
- No warnings about implementations in headers
- Ready to add new exchanges easily

---

## 🆘 Troubleshooting

### Build Error: "undefined reference to ClassName::method"
**Solution:** Method declared in .h but not implemented in .cpp. Add implementation.

### Build Error: "cannot find connector/hyperliquid_*.h"
**Solution:** Update include path to `connector/exchange/hyperliquid/hyperliquid_*.h`

### Build Error: "multiple definition of ClassName::method"
**Solution:** Implementation appears in both .h and .cpp. Remove from .h.

---

## 📊 Expected Timeline

- **Phase 1:** 30 minutes (fix 3 critical files)
- **Phase 2:** 10 minutes (run restructure script)
- **Phase 3:** 5 minutes (test & validate)

**Total:** ~45 minutes for production-ready architecture

---

## 🚀 Ready to Start?

```bash
# Start with the biggest file
code include/connector/hyperliquid_perpetual_connector.h
code src/connector/hyperliquid_perpetual_connector.cpp

# Remember: Cut implementation, paste in .cpp with ClassName:: prefix
# Then test: make -j4
```

**Questions?** See detailed status: `HEADER_IMPLEMENTATION_FIX_STATUS.md`

---

*Quick Start Guide | latentspeed C++ refactoring*
