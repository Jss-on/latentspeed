cmake_minimum_required(VERSION 3.20)

project(latentspeed VERSION 0.1.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(LATENTSPEED_WITH_ZLIB "Link ZLIB (needed for some ccapi exchanges)" OFF)
option(LATENTSPEED_WITH_PYTHON "Build Python bindings" ON)

# Required packages - use vcpkg packages to avoid system conflicts
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB QUIET)
find_package(CURL REQUIRED)
find_package(spdlog REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(RapidJSON REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

# Python bindings dependencies (optional)
if(LATENTSPEED_WITH_PYTHON)
  # Use system Python instead of vcpkg Python
  find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
  find_package(pybind11 REQUIRED)
  
  # Set Python variables for pybind11 compatibility
  set(Python_EXECUTABLE ${Python3_EXECUTABLE})
  set(Python_INCLUDE_DIRS ${Python3_INCLUDE_DIRS})
  set(Python_LIBRARIES ${Python3_LIBRARIES})
endif()

# Optional only when enabling FIX API
find_path(HFFIX_INCLUDE_DIR
  NAMES hffix/hffix.hpp
  PATH_SUFFIXES include
)

# ccapi interface target
add_library(latentspeed_ccapi INTERFACE)

# Include directories: ccapi headers + deps
target_include_directories(latentspeed_ccapi INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/ccapi/include
)
if(HFFIX_INCLUDE_DIR)
  target_include_directories(latentspeed_ccapi INTERFACE ${HFFIX_INCLUDE_DIR})
endif()

# Link libraries
target_link_libraries(latentspeed_ccapi INTERFACE 
  OpenSSL::SSL 
  OpenSSL::Crypto 
  Threads::Threads 
  ${ZMQ_LIBRARIES} 
  CURL::libcurl
  Boost::system
  rapidjson
)

if(WIN32)
  target_link_libraries(latentspeed_ccapi INTERFACE ws2_32)
endif()

if(ZLIB_FOUND AND LATENTSPEED_WITH_ZLIB)
  target_link_libraries(latentspeed_ccapi INTERFACE ZLIB::ZLIB)
endif()

# Default ccapi compile definitions (customize via -DCCAPI_COMPILE_DEFS=...)
set(CCAPI_COMPILE_DEFS
  CCAPI_ENABLE_SERVICE_MARKET_DATA
  CCAPI_ENABLE_EXCHANGE_BYBIT
  CACHE STRING "List of ccapi macros to enable features/exchanges"
)

target_compile_definitions(latentspeed_ccapi INTERFACE ${CCAPI_COMPILE_DEFS})

# Propagate standard and sensible flags
target_compile_features(latentspeed_ccapi INTERFACE cxx_std_17)
if(NOT MSVC)
  target_compile_options(latentspeed_ccapi INTERFACE -pthread)
endif()

message(STATUS "Configured latentspeed_ccapi. CCAPI_COMPILE_DEFS=${CCAPI_COMPILE_DEFS}")

# Trading Engine Service executable
add_executable(trading_engine_service
  src/main.cpp
  src/trading_engine_service.cpp
)

target_include_directories(trading_engine_service PRIVATE include)
target_link_libraries(trading_engine_service PRIVATE latentspeed_ccapi spdlog::spdlog)

# Python bindings module (optional)
if(LATENTSPEED_WITH_PYTHON)
  pybind11_add_module(latentspeed_py 
    src/python_bindings.cpp
    src/trading_engine_service.cpp
  )
  
  target_include_directories(latentspeed_py PRIVATE include)
  target_link_libraries(latentspeed_py PRIVATE latentspeed_ccapi spdlog::spdlog)
  
  # Set module properties
  target_compile_definitions(latentspeed_py PRIVATE VERSION_INFO=${PROJECT_VERSION})
  set_target_properties(latentspeed_py PROPERTIES
    OUTPUT_NAME "latentspeed"
    SUFFIX ${PYTHON_MODULE_EXTENSION}
  )
  
  message(STATUS "Python bindings enabled for latentspeed")
endif()
