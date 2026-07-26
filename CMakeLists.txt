cmake_minimum_required(VERSION 3.10)
project(logger
    VERSION 0.1.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include directories
set(LOG_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Library target
add_library(log
    STATIC
    src/log.c
)
target_include_directories(log
    PUBLIC
        ${LOG_INCLUDE_DIR}
)

# Example executable
add_executable(log_example
    examples/app.c
)
target_link_libraries(log_example
    PRIVATE
        log
)

# Tests (optional, enabled by default)
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()

    # Build the munit library from source
    add_library(munit
        STATIC
        deps/munit-0.2.0/munit.c
    )
    target_include_directories(munit
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/deps/munit-0.2.0
    )

    # Test executable
    add_executable(test_log
        tests/test_log.c
    )
    target_link_libraries(test_log
        PRIVATE
            log
            munit
    )

    # Register test with CTest
    add_test(NAME test_log
        COMMAND test_log
    )
endif()
