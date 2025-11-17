# Connector Framework Library
# Reorganized following Hummingbot pattern: Core + Exchange-specific subdirectories

cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Core Connector Sources (exchange-agnostic)
# ============================================================================
set(CONNECTOR_CORE_SOURCES
    connector_base.cpp
    zmq_order_event_publisher.cpp
)

# Optional: Uncomment if these files exist
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/client_order_tracker.cpp")
    list(APPEND CONNECTOR_CORE_SOURCES client_order_tracker.cpp)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/in_flight_order.cpp")
    list(APPEND CONNECTOR_CORE_SOURCES in_flight_order.cpp)
endif()

# ============================================================================
# Hyperliquid Exchange Implementation
# ============================================================================
# Explicitly list Hyperliquid connector sources (excluding incomplete stubs)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/exchange/hyperliquid")
    set(HYPERLIQUID_CONNECTOR_SOURCES
        exchange/hyperliquid/hyperliquid_auth.cpp
        exchange/hyperliquid/hyperliquid_perpetual_connector.cpp
        exchange/hyperliquid/hyperliquid_user_stream_data_source.cpp
        exchange/hyperliquid/hyperliquid_order_book_data_source.cpp
        exchange/hyperliquid/hyperliquid_marketstream_adapter.cpp
        # NOTE: hyperliquid_web_utils is header-only (no .cpp)
        # NOTE: hyperliquid_integrated_connector.cpp excluded - incomplete stub
    )
    message(STATUS "Hyperliquid connector sources: ${HYPERLIQUID_CONNECTOR_SOURCES}")
else()
    # Fallback for old structure (if files are in root)
    set(HYPERLIQUID_CONNECTOR_SOURCES
        hyperliquid_auth.cpp
        hyperliquid_perpetual_connector.cpp
        hyperliquid_user_stream_data_source.cpp
        hyperliquid_order_book_data_source.cpp
        hyperliquid_marketstream_adapter.cpp
    )
endif()

# ============================================================================
# Adapter Sources (required by Hyperliquid connector)
# ============================================================================
# Python signer bridge - required by HyperliquidAuth
set(ADAPTER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/adapters/python_hl_signer.cpp
)

# ============================================================================
# Combined Connector Library
# ============================================================================
add_library(connector_framework STATIC
    ${CONNECTOR_CORE_SOURCES}
    ${HYPERLIQUID_CONNECTOR_SOURCES}
    ${ADAPTER_SOURCES}
)

# Alternative: Use GLOB for automatic discovery (useful for development)
# Uncomment if you prefer automatic file discovery:
# file(GLOB_RECURSE HYPERLIQUID_CONNECTOR_SOURCES exchange/hyperliquid/*.cpp)

target_include_directories(connector_framework
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# ============================================================================
# Dependencies
# ============================================================================
find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)  # Required by python_hl_signer
# Boost: Only need header-only components (asio, beast)
# We use std::thread (C++20) instead of boost::thread
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(cppzmq CONFIG REQUIRED)

target_link_libraries(connector_framework
    PUBLIC
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        rapidjson
        Boost::boost
        Boost::system
        OpenSSL::SSL
        OpenSSL::Crypto
        cppzmq
        Threads::Threads  # Use standard C++ threads
)

# ============================================================================
# Compiler Settings
# ============================================================================
# Require C++20
target_compile_features(connector_framework PUBLIC cxx_std_20)

# Enable warnings
if(MSVC)
    target_compile_options(connector_framework PRIVATE /W4)
else()
    target_compile_options(connector_framework PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ============================================================================
# Installation
# ============================================================================
install(TARGETS connector_framework
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install headers (preserving directory structure)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/connector
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)
