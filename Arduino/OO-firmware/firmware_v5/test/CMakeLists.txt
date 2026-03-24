cmake_minimum_required(VERSION 3.14)
project(firmware_v5_tests)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# GoogleTest requires at least C++11

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mocks)

# Add subdirectories
add_subdirectory(mocks)

# Download and configure GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Test executable
add_executable(test_runner
  main.cpp
  tests/test_record_control.cpp
  tests/test_sequence_control.cpp
  tests/test_validation.cpp
  tests/test_helpers.cpp
  tests/test_key_control.cpp
  tests/test_master_control.cpp
  tests/test_slave_control.cpp
  include/record_control_testable.cpp
 tests/test_wifi.cpp
 tests/test_communication.cpp
 tests/test_firmware_v5.cpp
  include/sequence_control_testable.cpp
  include/validation_testable.cpp
  include/helpers_testable.cpp
  include/key_control_testable.cpp
  include/master_control_testable.cpp
  include/slave_control_testable.cpp
  include/globals.cpp
 include/wifi_testable.cpp
 include/communication_testable.cpp
 include/firmware_v5_testable.cpp
  include/hardware_stubs.cpp
)

# Link libraries
target_link_libraries(test_runner
  mocks
  gtest
  gtest_main
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(test_runner)

# Enable testing
enable_testing()
