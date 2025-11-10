# MarketStream library - Market data provider components
# This library is used by both the marketstream executable and trading_engine_service

add_library(marketstream_lib OBJECT
    feed_handler.cpp
    market_data_provider.cpp
    exchange_interface.cpp
)

# Use BEFORE to prioritize our includes over system includes
target_include_directories(marketstream_lib BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Link required libraries to ensure proper include paths
target_link_libraries(marketstream_lib PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::system
    spdlog::spdlog
    yaml-cpp::yaml-cpp
)
