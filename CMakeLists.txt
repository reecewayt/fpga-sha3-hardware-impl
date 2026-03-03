cmake_minimum_required(VERSION 3.10)
project(fpga-sha3-hardware-impl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Verilator
find_package(verilator HINTS $ENV{VERILATOR_ROOT})

if (NOT verilator_FOUND)
  message(FATAL_ERROR "Verilator not found. Please install Verilator or set VERILATOR_ROOT environment variable")
endif()

message(STATUS "Found Verilator: ${verilator_DIR}")

# Set RTL directory path for use in subdirectories
set(RTL_DIR ${CMAKE_SOURCE_DIR}/rtl)

# Enable CTest
enable_testing()

# Add tests subdirectory
add_subdirectory(tests)
