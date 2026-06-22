cmake_minimum_required(VERSION 3.14)
project(firmware_v5_tests)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Test executable
add_executable(test_runner
 main.cpp
 tests/test_record_control.cpp
 tests/test_sequence_control.cpp
 tests/test_master_control.cpp
 tests/test_validation.cpp
 tests/test_helpers.cpp
 tests/test_key_control.cpp
 tests/test_slave_control.cpp
 tests/test_communication.cpp
 tests/test_firmware_v5.cpp
 tests/test_chord_synthesis.cpp
 tests/test_chord_synthesis.cpp
 include/record_control_testable.cpp
 include/sequence_control_testable.cpp
 include/validation_testable.cpp
 include/helpers_testable.cpp
 include/key_control_testable.cpp
 include/master_control_testable.cpp
 include/slave_control_testable.cpp
 include/globals.cpp
 include/hardware_stubs.cpp
 include/wifi_testable.cpp
 include/communication_testable.cpp
 include/firmware_v5_testable.cpp
 include/chord_synthesis_testable.cpp
)

# Link libraries
target_link_libraries(test_runner PRIVATE
 mocks
 gtest
 gtest_main
)

# Coverage (must be after target is defined)
option(ENABLE_COVERAGE "Enable code coverage" OFF)
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_runner PRIVATE --coverage)
        target_link_libraries(test_runner PRIVATE --coverage)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(test_runner PRIVATE /Zi)
        target_link_options(test_runner PRIVATE /DEBUG:FULL)
    endif()
endif()

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(test_runner)

# Enable testing
enable_testing()
