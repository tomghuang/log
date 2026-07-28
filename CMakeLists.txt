cmake_minimum_required(VERSION 3.10)
project(logger)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Example executable
add_executable(log_example
  example.c
  log.c
)

# Tests (optional, enabled by default)
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
  enable_testing()

  add_executable(test_log
    log.c
    tests/test_log.c
    tests/munit-0.2.0/munit.c
  )

  target_include_directories(test_log PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/munit-0.2.0
  )

  add_test(NAME test_log COMMAND test_log)
endif()
