cmake_minimum_required(VERSION 3.20)

project(latentspeed VERSION 0.1.0 LANGUAGES CXX)

# Enable CTest even though we keep dependencies minimal
include(CTest)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# HFT Compiler optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG -march=native -mtune=native")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto -ffast-math -funroll-loops")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -falign-functions=32 -falign-loops=32")
endif()

# Required packages
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(spdlog REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(RapidJSON REQUIRED)
find_package(args REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

# WebSocket++ is header-only, no find_package needed
# It will be available through vcpkg include directories

# Add subdirectories
add_subdirectory(benchmark)

# Trading Engine Service executable
add_executable(trading_engine_service
  src/main.cpp
  src/trading_engine_service.cpp
  src/market_data_provider.cpp
  src/exchange/exchange_client.cpp
  src/exchange/bybit_client.cpp
  src/exchange/binance_client.cpp
)

# Market Data Test executable
add_executable(test_market_data
  src/test_market_data.cpp
  src/market_data_provider.cpp
)

target_include_directories(trading_engine_service PRIVATE include)
target_include_directories(test_market_data PRIVATE include)

# Header-only libraries
if(TARGET args)
    target_link_libraries(trading_engine_service PRIVATE args)
endif()

# HFT-optimized link libraries
target_link_libraries(trading_engine_service PRIVATE 
  OpenSSL::SSL 
  OpenSSL::Crypto 
  Threads::Threads 
  ${ZMQ_LIBRARIES} 
  CURL::libcurl
  Boost::system
  rapidjson
  spdlog::spdlog
)

# Test Market Data executable libraries
target_link_libraries(test_market_data PRIVATE 
  OpenSSL::SSL 
  OpenSSL::Crypto 
  Threads::Threads 
  ${ZMQ_LIBRARIES} 
  Boost::system
  rapidjson
  spdlog::spdlog
)

# Linux-specific libraries for HFT features
if(UNIX AND NOT APPLE)
    target_link_libraries(trading_engine_service PRIVATE 
        pthread  # POSIX threads for CPU affinity
        rt       # Real-time extensions for scheduling
    )
    
    target_link_libraries(test_market_data PRIVATE 
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

if(BUILD_TESTING)
    add_executable(action_dispatch_tests
        tests/action_dispatch_test.cpp
    )
    target_include_directories(action_dispatch_tests PRIVATE include)
    add_test(NAME action_dispatch_tests COMMAND action_dispatch_tests)
endif()
