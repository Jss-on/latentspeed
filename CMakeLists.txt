cmake_minimum_required(VERSION 3.20)
# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# HFT Compiler optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG -march=native -mtune=native")
    # TEMPORARY: Disable LTO due to system/vcpkg library conflicts
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffast-math -funroll-loops")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -falign-functions=32 -falign-loops=32")
endif()

# Required packages
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(RapidJSON REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(args REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)


# MarketStream library
add_subdirectory(src/marketstream)

# Phase 1: Connector Framework (Hummingbot Architecture)
add_subdirectory(src/connector)

# Tests (if enabled)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests/unit/connector)
endif()

# Trading Engine Service executable
set(LS_ENGINE_SOURCES
  src/main_trading_engine.cpp
  src/trading_engine_service.cpp
  src/utils/string_utils.cpp
  src/utils/hft_utils.cpp
  src/engine/cli_config.cpp
  src/engine/order_serializer.cpp
  src/exchange/exchange_client.cpp
  src/adapters/hyperliquid_adapter.cpp
  src/adapters/hyperliquid/hyperliquid_connector_adapter.cpp
  src/adapters/hyperliquid_asset_resolver.cpp
  src/adapters/python_hl_signer.cpp
  src/core/symbol/symbol_mapper.cpp
  src/core/reasons/reason_mapper.cpp
  src/engine/exec_dto.cpp
  src/core/net/http_client.cpp
  src/core/net/hl_ws_post_client.cpp
  src/core/auth/bybit_auth_provider.cpp
  src/core/auth/credentials_resolver.cpp
)

add_executable(trading_engine_service ${LS_ENGINE_SOURCES} $<TARGET_OBJECTS:marketstream_lib>)

# MarketStream - Production Market Data Provider (YAML-based)
add_executable(marketstream
  src/marketstream.cpp
  $<TARGET_OBJECTS:marketstream_lib>
)

target_include_directories(trading_engine_service BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(marketstream BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/include)

if(TARGET args)
    target_link_libraries(trading_engine_service PRIVATE args)
endif()

# HFT-optimized link libraries
target_link_libraries(trading_engine_service PRIVATE 
  connector_framework  # Hummingbot-pattern connector library
  OpenSSL::SSL 
  OpenSSL::Crypto
  ZLIB::ZLIB
  Threads::Threads 
  ${ZMQ_LIBRARIES} 
  CURL::libcurl
  Boost::system
  rapidjson
  yaml-cpp::yaml-cpp
  spdlog::spdlog
)

# MarketStream executable libraries
target_link_libraries(marketstream PRIVATE 
  OpenSSL::SSL 
  OpenSSL::Crypto
  ZLIB::ZLIB
  Threads::Threads 
  ${ZMQ_LIBRARIES} 
  Boost::system
  rapidjson
  yaml-cpp::yaml-cpp
  spdlog::spdlog
)

# Linux-specific libraries for HFT features
if(UNIX AND NOT APPLE)
    target_link_libraries(trading_engine_service PRIVATE 
        pthread  # POSIX threads for CPU affinity
        rt       # Real-time extensions for scheduling
    )
    
    target_link_libraries(marketstream PRIVATE 
        pthread  # POSIX threads for CPU affinity
        rt       # Real-time extensions for scheduling
    )
    
    # Enable HFT-specific compiler features on Linux
    target_compile_definitions(trading_engine_service PRIVATE 
        _GNU_SOURCE          # Enable GNU extensions for CPU affinity
        HFT_LINUX_FEATURES   # Enable HFT Linux-specific code paths
    )
endif()

# Compiler-specific optimizations for HFT
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(trading_engine_service PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter 
        -Wno-deprecated-declarations 
        -Wno-sign-compare
        $<$<CONFIG:Release>:-fno-stack-protector>  # Disable stack protection in release for max performance
        $<$<CONFIG:Release>:-fno-asynchronous-unwind-tables>  # Reduce binary size
    )
endif()

# Link-time optimizations for HFT release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(trading_engine_service PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION TRUE  # Enable LTO
    )
endif()

