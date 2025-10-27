# Directory Restructuring Plan - Hummingbot Pattern

## Current Structure (WRONG ❌)
```
include/connector/
├── connector_base.h                           # ✅ Core - stays here
├── client_order_tracker.h                     # ✅ Core - stays here
├── in_flight_order.h                          # ✅ Core - stays here
├── hyperliquid_integrated_connector.h         # ❌ Should be in exchange/hyperliquid/
├── hyperliquid_perpetual_connector.h          # ❌ Should be in exchange/hyperliquid/
├── hyperliquid_user_stream_data_source.h      # ❌ Should be in exchange/hyperliquid/
├── hyperliquid_order_book_data_source.h       # ❌ Should be in exchange/hyperliquid/
├── hyperliquid_auth.h                         # ❌ Should be in exchange/hyperliquid/
├── hyperliquid_marketstream_adapter.h         # ❌ Should be in exchange/hyperliquid/
├── hyperliquid_web_utils.h                    # ❌ Should be in exchange/hyperliquid/
├── zmq_order_event_publisher.h                # ✅ Core - stays here
├── order_book.h                               # ✅ Core - stays here
├── position.h                                 # ✅ Core - stays here
└── trading_rule.h                             # ✅ Core - stays here

src/connector/
├── connector_base.cpp                         # ✅ Core - stays here
├── hyperliquid_integrated_connector.cpp       # ❌ Should be in exchange/hyperliquid/
├── hyperliquid_perpetual_connector.cpp        # ❌ Should be in exchange/hyperliquid/
└── ...
```

## Target Structure (Hummingbot Pattern) ✅

```
include/connector/
├── connector_base.h                           # Base interface
├── client_order_tracker.h                     # Order tracking
├── in_flight_order.h                          # Order state
├── types.h                                    # Common types
├── events.h                                   # Event system
├── zmq_order_event_publisher.h                # Event publisher
├── order_book.h                               # OrderBook data structure
├── position.h                                 # Position tracking
├── trading_rule.h                             # Trading rules
├── order_book_tracker_data_source.h           # Base class
├── user_stream_tracker_data_source.h          # Base class
└── exchange/                                  # 🆕 Exchange-specific implementations
    ├── hyperliquid/                           # 🆕 Hyperliquid subdirectory
    │   ├── hyperliquid_exchange.h             # Main connector (rename from perpetual_connector)
    │   ├── hyperliquid_integrated_connector.h # Alternative implementation
    │   ├── hyperliquid_api_order_book_data_source.h
    │   ├── hyperliquid_api_user_stream_data_source.h
    │   ├── hyperliquid_auth.h
    │   ├── hyperliquid_constants.h            # 🆕 URLs, rate limits
    │   ├── hyperliquid_web_utils.h
    │   ├── hyperliquid_marketstream_adapter.h
    │   └── hyperliquid_order_book.h           # 🆕 If needed
    └── bybit/                                 # 🆕 Future: Bybit subdirectory
        ├── bybit_exchange.h
        ├── bybit_api_order_book_data_source.h
        ├── bybit_api_user_stream_data_source.h
        ├── bybit_auth.h
        ├── bybit_constants.h
        └── bybit_web_utils.h

src/connector/
├── connector_base.cpp
├── client_order_tracker.cpp
├── in_flight_order.cpp
├── zmq_order_event_publisher.cpp
└── exchange/                                  # 🆕 Exchange implementations
    ├── hyperliquid/                           # 🆕
    │   ├── hyperliquid_exchange.cpp
    │   ├── hyperliquid_integrated_connector.cpp
    │   ├── hyperliquid_api_order_book_data_source.cpp
    │   ├── hyperliquid_api_user_stream_data_source.cpp
    │   ├── hyperliquid_auth.cpp
    │   └── hyperliquid_web_utils.cpp
    └── bybit/                                 # 🆕 Future
        └── bybit_exchange.cpp
```

## Benefits

1. **Isolation** - Each exchange is self-contained
2. **Scalability** - Easy to add new exchanges
3. **Clarity** - Clear which files belong to which exchange
4. **Hummingbot Compatibility** - Same pattern = easier to reference
5. **Compilation** - Can compile exchanges independently

## Migration Steps

### Phase 1: Create Directory Structure
```bash
mkdir -p include/connector/exchange/hyperliquid
mkdir -p src/connector/exchange/hyperliquid
```

### Phase 2: Move Files
```bash
# Move headers
mv include/connector/hyperliquid_*.h include/connector/exchange/hyperliquid/

# Move implementations
mv src/connector/hyperliquid_*.cpp src/connector/exchange/hyperliquid/

# Rename for consistency with Hummingbot
mv include/connector/exchange/hyperliquid/hyperliquid_perpetual_connector.h \
   include/connector/exchange/hyperliquid/hyperliquid_exchange.h

mv src/connector/exchange/hyperliquid/hyperliquid_perpetual_connector.cpp \
   src/connector/exchange/hyperliquid/hyperliquid_exchange.cpp
```

### Phase 3: Update Includes
```cpp
// OLD:
#include "connector/hyperliquid_auth.h"

// NEW:
#include "connector/exchange/hyperliquid/hyperliquid_auth.h"
```

### Phase 4: Update CMakeLists.txt
```cmake
# Add subdirectories
add_subdirectory(src/connector/exchange/hyperliquid)

# Or in main CMakeLists.txt:
file(GLOB_RECURSE HYPERLIQUID_SOURCES 
    "src/connector/exchange/hyperliquid/*.cpp"
)

add_library(latentspeed_connector
    src/connector/connector_base.cpp
    src/connector/client_order_tracker.cpp
    ${HYPERLIQUID_SOURCES}
)
```

## File Mapping

| Old Location | New Location |
|-------------|--------------|
| `include/connector/hyperliquid_auth.h` | `include/connector/exchange/hyperliquid/hyperliquid_auth.h` |
| `include/connector/hyperliquid_perpetual_connector.h` | `include/connector/exchange/hyperliquid/hyperliquid_exchange.h` |
| `include/connector/hyperliquid_user_stream_data_source.h` | `include/connector/exchange/hyperliquid/hyperliquid_api_user_stream_data_source.h` |
| `include/connector/hyperliquid_order_book_data_source.h` | `include/connector/exchange/hyperliquid/hyperliquid_api_order_book_data_source.h` |
| Same for src/ files | Same pattern |

## Next: Do the Same for Bybit, Binance, etc.
