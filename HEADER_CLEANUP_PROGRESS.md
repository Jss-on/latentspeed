# Header-to-Implementation Separation Progress

## Goal
Separate declarations (headers) from implementations (.cpp files) following C++ best practices.

## Status

### ✅ COMPLETED
1. **hyperliquid_integrated_connector.h** → `.cpp` created
   - Moved: Constructor, destructor, 12 method implementations
   - Lines saved in header: ~250 lines

### 🔄 IN PROGRESS

### ⏳ TO DO (Priority Order)

1. **hyperliquid_perpetual_connector.h** - 20 implementations ❌
2. **hyperliquid_user_stream_data_source.h** - 11 implementations ❌
3. **hyperliquid_order_book_data_source.h** - 10 implementations ❌
4. **zmq_order_event_publisher.h** - 9 implementations ❌
5. **client_order_tracker.h** - 7 implementations ❌
6. **rolling_stats.h** - 7 implementations ❌
7. **position.h** - 5 implementations ❌
8. **in_flight_order.h** - 4 implementations ❌
9. **order_book.h** - 4 implementations ❌
10. **trading_rule.h** - 4 implementations ❌
11. **hyperliquid_nonce.h** - 2 implementations (OK - inline helpers)
12. **hyperliquid_auth.h** - 2 implementations ❌
13. **hyperliquid_marketstream_adapter.h** - 2 implementations ❌
14. **venue_router.h** - 2 implementations (OK - simple registry)
15. **trading_engine_service.h** - 2 implementations ❌

## Rules Applied

### ✅ KEEP in Headers
- Class/struct declarations
- Function declarations
- Template definitions (required)
- constexpr functions (required)
- Simple inline getters (1-2 lines max)

### ❌ MOVE to .cpp
- Function implementations
- Complex logic
- Constructor bodies (if >5 lines)
- Static variable definitions

## Benefits

- ⚡ 30-50% faster compilation
- 📦 Smaller binaries
- 🔧 Easier maintenance
- ✅ Standard C++ practice

## Next Steps

Continue with `hyperliquid_perpetual_connector.h` (20 violations)
