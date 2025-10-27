# Directory Restructuring Checklist

## 🎯 Goal
Reorganize connector files into Hummingbot pattern with exchange-specific subdirectories.

---

## ✅ Pre-Flight Checklist

- [ ] **Backup current state**
  ```bash
  git add -A
  git commit -m "backup: before restructure"
  # OR create a branch
  git checkout -b feature/restructure-connectors
  ```

- [ ] **Verify clean working directory**
  ```bash
  git status
  # Should show no uncommitted changes (except new files)
  ```

- [ ] **Review files to be moved**
  - Headers: 7 hyperliquid_*.h files
  - Implementation: 5 hyperliquid_*.cpp files

---

## 🚀 Execution Steps

### Step 1: Run Restructure Script

```bash
cd /path/to/latentspeed
bash tools/execute_restructure.sh
```

**Expected Output:**
- ✅ Moved 7 header files
- ✅ Moved 5 implementation files  
- ✅ Updated include paths in all files

### Step 2: Verify File Locations

Check that files moved correctly:

```bash
# Headers should be in exchange/hyperliquid/
ls -la include/connector/exchange/hyperliquid/

# Should show:
# - hyperliquid_auth.h
# - hyperliquid_integrated_connector.h
# - hyperliquid_marketstream_adapter.h
# - hyperliquid_order_book_data_source.h
# - hyperliquid_perpetual_connector.h
# - hyperliquid_user_stream_data_source.h
# - hyperliquid_web_utils.h

# Implementation files should be in exchange/hyperliquid/
ls -la src/connector/exchange/hyperliquid/

# Should show:
# - hyperliquid_auth.cpp
# - hyperliquid_integrated_connector.cpp
# - hyperliquid_order_book_data_source.cpp
# - hyperliquid_perpetual_connector.cpp
# - hyperliquid_user_stream_data_source.cpp
```

### Step 3: Verify Include Paths Updated

Check that old paths are replaced:

```bash
# Should find NO results (old path removed)
grep -r '#include "connector/hyperliquid_' include/connector/*.h || echo "✅ No old paths in core headers"
grep -r '#include "connector/hyperliquid_' src/connector/*.cpp || echo "✅ No old paths in core cpp"

# Should find results (new path used)
grep -r '#include "connector/exchange/hyperliquid/' include/connector/exchange/hyperliquid/ && echo "✅ New paths in hyperliquid files"
```

### Step 4: Update CMakeLists.txt

Open `src/connector/CMakeLists.txt` and update to:

```cmake
# Connector Core Library
set(CONNECTOR_CORE_SOURCES
    connector_base.cpp
    client_order_tracker.cpp
    in_flight_order.cpp
    zmq_order_event_publisher.cpp
)

# Hyperliquid Exchange Implementation  
file(GLOB_RECURSE HYPERLIQUID_CONNECTOR_SOURCES
    exchange/hyperliquid/*.cpp
)

# Combined Connector Library
add_library(latentspeed_connector STATIC
    ${CONNECTOR_CORE_SOURCES}
    ${HYPERLIQUID_CONNECTOR_SOURCES}
)

target_include_directories(latentspeed_connector PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(latentspeed_connector
    PUBLIC
        Boost::boost
        Boost::system
        Boost::thread
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        libzmq
)
```

### Step 5: Test Compilation

```bash
cd build
cmake ..
make clean
make -j$(nproc)
```

**Expected:** Clean compilation with no errors

### Step 6: Run Tests (if available)

```bash
cd build
ctest
# OR
./tests/connector_tests
```

---

## 🔍 Verification Checklist

After restructuring, verify:

- [ ] **All files moved successfully**
  - No hyperliquid_*.h in `include/connector/` (root)
  - No hyperliquid_*.cpp in `src/connector/` (root)
  - All hyperliquid files in `exchange/hyperliquid/` subdirs

- [ ] **Include paths updated**
  - Old: `#include "connector/hyperliquid_auth.h"`
  - New: `#include "connector/exchange/hyperliquid/hyperliquid_auth.h"`
  - Search entire codebase for old patterns

- [ ] **Compilation succeeds**
  ```bash
  make -j$(nproc)  # No errors
  ```

- [ ] **No broken includes**
  ```bash
  # Check for any remaining old-style includes
  grep -r "connector/hyperliquid_" include/ src/ examples/ --include="*.h" --include="*.cpp"
  # Should only show files in exchange/hyperliquid/
  ```

- [ ] **Core files remain in place**
  - `connector_base.h` - still in `include/connector/`
  - `client_order_tracker.h` - still in `include/connector/`
  - `in_flight_order.h` - still in `include/connector/`
  - `events.h`, `types.h` - still in `include/connector/`

---

## 📊 Expected Structure

### Before
```
include/connector/
├── connector_base.h
├── hyperliquid_auth.h               ❌ Mixed
├── hyperliquid_integrated_connector.h ❌ Mixed
└── client_order_tracker.h
```

### After
```
include/connector/
├── connector_base.h                 ✅ Core
├── client_order_tracker.h           ✅ Core
├── events.h                         ✅ Core
├── types.h                          ✅ Core
└── exchange/                        ✅ Isolated
    └── hyperliquid/
        ├── hyperliquid_auth.h
        ├── hyperliquid_integrated_connector.h
        ├── hyperliquid_perpetual_connector.h
        ├── hyperliquid_user_stream_data_source.h
        ├── hyperliquid_order_book_data_source.h
        ├── hyperliquid_marketstream_adapter.h
        └── hyperliquid_web_utils.h
```

---

## 🐛 Troubleshooting

### Issue: "fatal error: connector/hyperliquid_auth.h: No such file"

**Cause:** Include path not updated  
**Fix:**
```bash
# Find and replace in the offending file
sed -i 's|connector/hyperliquid_|connector/exchange/hyperliquid/hyperliquid_|g' <filename>
```

### Issue: "undefined reference to HyperliquidAuth::method"

**Cause:** CMakeLists.txt not updated with new paths  
**Fix:** Ensure CMakeLists.txt includes `exchange/hyperliquid/*.cpp`

### Issue: Script fails with "Permission denied"

**Cause:** Script not executable  
**Fix:**
```bash
chmod +x tools/execute_restructure.sh
```

### Issue: Git mv fails

**Cause:** Files not tracked by git  
**Fix:** Script will fall back to regular `mv` command automatically

---

## ✅ Success Criteria

- [x] All Hyperliquid files in `exchange/hyperliquid/` subdirectory
- [x] No Hyperliquid files in root `connector/` directory
- [x] All include paths updated to new structure
- [x] Clean compilation (`make -j$(nproc)` succeeds)
- [x] Tests pass (if available)
- [x] Core connector files unchanged (connector_base.h, etc.)

---

## 🎉 Completion

Once all checks pass:

```bash
# Commit the restructured codebase
git add -A
git commit -m "refactor: restructure connectors to Hummingbot pattern

- Move Hyperliquid files to connector/exchange/hyperliquid/
- Update all include paths
- Separate exchange-specific code from core connectors
- Ready to add Bybit, Binance, etc. following same pattern"

# Optional: Push to remote
git push origin feature/restructure-connectors
```

---

**Status:** Ready to execute  
**Time:** ~5 minutes  
**Risk:** Low (git tracked, easy to revert)

Run: `bash tools/execute_restructure.sh`
