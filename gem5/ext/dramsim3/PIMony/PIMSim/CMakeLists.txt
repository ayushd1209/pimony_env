cmake_minimum_required(VERSION 3.0.0)
project(pimsim)

set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

add_library(inih INTERFACE)
target_include_directories(inih INTERFACE ext/headers)

add_library(format INTERFACE)
target_include_directories(format INTERFACE ext/fmt/include)
target_compile_definitions(format INTERFACE FMT_HEADER_ONLY=1)

# argparsing library, only used in main program not the library
add_library(args INTERFACE)
target_include_directories(args INTERFACE ext/headers)

add_library(json INTERFACE)
target_include_directories(json INTERFACE ext/headers)

# Main DRAMSim Lib
add_library(pimsim STATIC
    src/bankstate.cc
    src/channel_state.cc
    src/command_queue.cc
    src/common.cc
    src/configuration.cc
    src/dram_system.cc
    src/PIMSim.cc
    src/refresh.cc
    src/simple_stats.cc
    src/timing.cc
    src/pim_command_queue.cc
    src/pim_controller.cc
)

if (CMD_TRACE)
    target_compile_options(pimsim PRIVATE -DCMD_TRACE)
endif (CMD_TRACE)

if (ADDR_TRACE)
    target_compile_options(pimsim PRIVATE -DADDR_TRACE)
endif (ADDR_TRACE)


target_include_directories(pimsim PRIVATE
    include
    include/pimsim
    src
)

target_compile_options(pimsim PRIVATE -Wall)
target_link_libraries(pimsim PRIVATE inih format)
set_target_properties(pimsim PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

# Unit testing
add_library(Catch INTERFACE)
target_include_directories(Catch INTERFACE ext/headers)
