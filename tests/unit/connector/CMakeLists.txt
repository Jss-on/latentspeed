# Unit tests for connector framework

find_package(GTest CONFIG REQUIRED)

# Test executable
add_executable(test_connector_base
    test_connector_base.cpp
)

target_link_libraries(test_connector_base
    PRIVATE
        connector_framework
        GTest::gtest
        GTest::gtest_main
)

# Add to CTest
include(GoogleTest)
gtest_discover_tests(test_connector_base)
