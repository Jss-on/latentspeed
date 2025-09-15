# HFT Benchmark executable
add_executable(hft_benchmark
    hft_benchmark.cpp
)

# Link against required libraries
target_link_libraries(hft_benchmark PRIVATE
    ${CMAKE_THREAD_LIBS_INIT}
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Include directories
target_include_directories(hft_benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/exchange
)

# Compiler-specific optimizations for benchmark accuracy
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(hft_benchmark PRIVATE
        -march=native
        -mtune=native
        -O3
        -DNDEBUG
        -flto
        -ffast-math
        -funroll-loops
        -falign-functions=32
        -falign-loops=32
        -fno-stack-protector
        -fno-asynchronous-unwind-tables
    )
    
    target_link_options(hft_benchmark PRIVATE
        -flto
        -fno-fat-lto-objects
    )
endif()

# Enable all warnings for benchmark code quality
target_compile_options(hft_benchmark PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Wno-deprecated-declarations
    -Wno-sign-compare
)
