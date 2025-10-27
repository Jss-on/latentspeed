# 🚀 Restructure Files NOW - Simple Guide

## Quick Start (5 minutes)

### Option 1: Automated Script (Recommended)

```bash
# Navigate to project
cd ~/latentspeed

# Run the restructure script
bash tools/execute_restructure.sh

# Update CMakeLists.txt (manual step, see below)
# Then compile
cd build
cmake ..
make -j4
```

### Option 2: Manual Steps

```bash
cd ~/latentspeed

# Move header files
mv include/connector/hyperliquid_*.h include/connector/exchange/hyperliquid/

# Move implementation files
mv src/connector/hyperliquid_*.cpp src/connector/exchange/hyperliquid/

# Update include paths (automatic with script, or manual search/replace)
# Old: #include "connector/hyperliquid_auth.h"
# New: #include "connector/exchange/hyperliquid/hyperliquid_auth.h"

# Compile and test
cd build && cmake .. && make -j4
```

---

## 📋 What Gets Moved

### Headers (7 files) → `include/connector/exchange/hyperliquid/`
- ✅ hyperliquid_auth.h
- ✅ hyperliquid_integrated_connector.h
- ✅ hyperliquid_marketstream_adapter.h
- ✅ hyperliquid_order_book_data_source.h
- ✅ hyperliquid_perpetual_connector.h
- ✅ hyperliquid_user_stream_data_source.h
- ✅ hyperliquid_web_utils.h

### Implementation (5 files) → `src/connector/exchange/hyperliquid/`
- ✅ hyperliquid_auth.cpp
- ✅ hyperliquid_integrated_connector.cpp
- ✅ hyperliquid_order_book_data_source.cpp
- ✅ hyperliquid_perpetual_connector.cpp
- ✅ hyperliquid_user_stream_data_source.cpp

### Stay in Place (Core)
- ✅ connector_base.h
- ✅ client_order_tracker.h
- ✅ in_flight_order.h
- ✅ events.h, types.h
- ✅ All .cpp for above

---

## 📝 Update CMakeLists.txt

Add to `src/connector/CMakeLists.txt`:

```cmake
# Hyperliquid Exchange Implementation
file(GLOB_RECURSE HYPERLIQUID_SOURCES
    exchange/hyperliquid/*.cpp
)

add_library(latentspeed_connector STATIC
    connector_base.cpp
    client_order_tracker.cpp
    in_flight_order.cpp
    zmq_order_event_publisher.cpp
    ${HYPERLIQUID_SOURCES}
)
```

---

## ✅ Verify Success

```bash
# Check files moved
ls include/connector/exchange/hyperliquid/
# Should show 7 .h files

ls src/connector/exchange/hyperliquid/
# Should show 5 .cpp files

# Verify core files still in place
ls include/connector/*.h
# Should show connector_base.h, client_order_tracker.h, etc.

# Test compilation
cd build
cmake ..
make -j4
# Should compile cleanly
```

---

## 🎯 Result

### Before
```
connector/
├── connector_base.h                  ✅ Core
├── hyperliquid_perpetual_connector.h ❌ Mixed together
└── hyperliquid_auth.h                ❌ Mixed together
```

### After
```
connector/
├── connector_base.h                  ✅ Core
├── client_order_tracker.h            ✅ Core
└── exchange/                         ✅ Clean separation
    └── hyperliquid/
        ├── hyperliquid_perpetual_connector.h
        ├── hyperliquid_auth.h
        └── ...
```

---

## 🚨 If Something Breaks

```bash
# Revert changes
git reset --hard HEAD
# OR
git checkout -- .

# Try again or ask for help
```

---

## ⚡ Ready? Run This:

```bash
cd ~/latentspeed
bash tools/execute_restructure.sh
```

**Time:** 2 minutes  
**Risk:** Low (git tracked)  
**Benefit:** Clean Hummingbot-style architecture

---

*Created: 2025-10-23*  
*Status: Ready to execute*
