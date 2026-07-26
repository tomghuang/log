cmake_minimum_required(VERSION 3.10)
project(logger
    VERSION 0.1.0
    # LANGUAGES C
)

# set(CMAKE_C_STANDARD 11)
# set(CMAKE_C_STANDARD_REQUIRED ON)

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

# Suppress MSVC deprecation warnings for fopen, strerror, localtime, etc.
if(MSVC)
    target_compile_definitions(log PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

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

    # On MSVC, don't enforce C11 for munit — MSVC's default C mode doesn't define
    # __STDC_VERSION__, so MUNIT_ARRAY_PARAM expands to nothing, avoiding VLA-style
    # array parameter syntax that MSVC's C compiler doesn't support.
    if(MSVC)
        set_target_properties(munit PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED OFF)
    endif()

    # Test executable
    add_executable(test_log
        tests/test_log.c
    )
    # Suppress MSVC deprecation warnings for test_log (strncpy, fileno, etc.)
    if(MSVC)
        target_compile_definitions(test_log PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_DEPRECATE
        )
    endif()

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
