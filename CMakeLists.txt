cmake_minimum_required(VERSION 3.20)

project(latentspeed VERSION 0.1.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Required packages
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(spdlog REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(RapidJSON REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

# Trading Engine Service executable
add_executable(trading_engine_service
  src/main.cpp
  src/trading_engine_service.cpp
  src/symbol_fetcher.cpp
  src/exchange/exchange_client.cpp
  src/exchange/bybit_client.cpp
)

target_include_directories(trading_engine_service PRIVATE include)
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

