
cmake_minimum_required(VERSION 3.15)

# Name of the static library built from source files under src/
set(LIB_NAME "Simulator")

# Collect all .cc source files under src/
file(GLOB_RECURSE SRC_CC
  "${CMAKE_SOURCE_DIR}/src/*.cc"
)

# Exclude files that should belong to other targets:
# - memory_system.cc is compiled only into the dramsim3 shared library
# - main.cpp is used only for the pimony executable
list(FILTER SRC_CC EXCLUDE REGEX ".*/memory_system\\.cc$")
list(FILTER SRC_CC EXCLUDE REGEX ".*/main\\.cpp$")

# Build the Simulator_lib target with the filtered sources
add_library(${LIB_NAME}_lib ${SRC_CC})

# Expose the src/ directory as a public include path for dependent targets
target_include_directories(${LIB_NAME}_lib PUBLIC
  ${CMAKE_SOURCE_DIR}/src
)

# (Optional) Add extra compile options for stability/sanitizing if desired
# target_compile_options(${LIB_NAME}_lib PRIVATE -fno-strict-aliasing -fno-omit-frame-pointer)
