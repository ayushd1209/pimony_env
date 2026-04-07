cmake_minimum_required(VERSION 3.12.0)
cmake_policy(SET CMP0079 NEW)
# cmake_policy(SET CMP0167 OLD)
set(project_name "mobilePIM")
project(${project_name})

# set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS program_options)
# find_package(boost REQUIRED COMPONENTS program_options)

include_directories(${PROJECT_SOURCE_DIR}/external)

include_directories(${PROJECT_SOURCE_DIR}/external/robin-hood-hashing/src/include)

add_subdirectory(external/fmt)
set_target_properties(fmt PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_subdirectory(external/robin-hood-hashing)
add_subdirectory(external/spdlog)
set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_subdirectory(external/json)

# Add PIMSim (DRAMSim3) library before defining executables
add_subdirectory(${PROJECT_SOURCE_DIR}/PIMSim)

set_target_properties(pimsim PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_subdirectory(${PROJECT_SOURCE_DIR}/src)

set_target_properties(Simulator_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(dramsim3 SHARED
    src/memory_system.cc
)

target_include_directories(dramsim3 PUBLIC
    ${PROJECT_SOURCE_DIR}/src                   # dramsim3.h, memory_system.h
    ${PROJECT_SOURCE_DIR}/PIMSim/include     
)

target_link_libraries(dramsim3 PUBLIC
    Simulator_lib
    pimsim
    fmt
    spdlog
    json
    ${Boost_LIBRARIES}
)

set_target_properties(dramsim3 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}
)


# Define the pimony binary
add_executable(pimony src/main.cpp)

# Add include paths if needed
target_include_directories(pimony PRIVATE
    ${Boost_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/PIMSim/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/external/robin-hood-hashing
)

# Link required libraries (from Conan or PIMSim)
target_link_libraries(pimony PRIVATE
    pimsim
    Simulator_lib
    fmt
    spdlog
    json
    ${Boost_LIBRARIES}
)

target_include_directories(Simulator_lib PUBLIC ${Boost_INCLUDE_DIRS})
target_link_libraries(Simulator_lib 
pimsim
fmt
spdlog
json
${Boost_LIBRARIES}
)
