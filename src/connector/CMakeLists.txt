# Connector Framework Library
# Phase 1: Core Architecture

cmake_minimum_required(VERSION 3.20)

# Create connector framework library
add_library(connector_framework
    connector_base.cpp
)

target_include_directories(connector_framework
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(connector_framework
    PUBLIC
        spdlog::spdlog
)

# Require C++20
target_compile_features(connector_framework PUBLIC cxx_std_20)

# Enable warnings
if(MSVC)
    target_compile_options(connector_framework PRIVATE /W4)
else()
    target_compile_options(connector_framework PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Install targets
install(TARGETS connector_framework
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install headers
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/connector
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)
