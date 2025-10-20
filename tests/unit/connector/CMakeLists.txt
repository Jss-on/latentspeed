# Unit tests for connector framework

find_package(GTest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Phase 1 tests
add_executable(test_connector_base
    test_connector_base.cpp
)

target_link_libraries(test_connector_base
    PRIVATE
        connector_framework
        GTest::gtest
        GTest::gtest_main
)

# Phase 2 tests
add_executable(test_order_tracking
    test_order_tracking.cpp
)

target_link_libraries(test_order_tracking
    PRIVATE
        connector_framework
        GTest::gtest
        GTest::gtest_main
)

# Phase 3 tests
add_executable(test_order_book
    test_order_book.cpp
)

target_link_libraries(test_order_book
    PRIVATE
        connector_framework
        GTest::gtest
        GTest::gtest_main
        nlohmann_json::nlohmann_json
)

# Phase 4 tests
add_executable(test_hyperliquid_utils
    test_hyperliquid_utils.cpp
)

target_link_libraries(test_hyperliquid_utils
    PRIVATE
        connector_framework
        GTest::gtest
        GTest::gtest_main
        nlohmann_json::nlohmann_json
)

# Add to CTest
include(GoogleTest)
gtest_discover_tests(test_connector_base)
gtest_discover_tests(test_order_tracking)
gtest_discover_tests(test_order_book)
gtest_discover_tests(test_hyperliquid_utils)
