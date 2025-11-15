cmake_minimum_required(VERSION 3.15)
project(gltfu VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Check if dependencies exist, if not download them
set(DEPS_MARKER "${CMAKE_CURRENT_SOURCE_DIR}/third_party/.deps_downloaded")
if(NOT EXISTS "${DEPS_MARKER}")
    message(STATUS "Dependencies not found, downloading...")
    execute_process(
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/download_deps.sh"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE DOWNLOAD_RESULT
    )
    if(DOWNLOAD_RESULT EQUAL 0)
        file(WRITE "${DEPS_MARKER}" "Dependencies downloaded")
        message(STATUS "Dependencies downloaded successfully")
    else()
        message(FATAL_ERROR "Failed to download dependencies. Please run ./download_deps.sh manually.")
    endif()
endif()

# All dependencies are header-only libraries in third_party/
# No need for vcpkg or find_package

# Add tinygltf as a header-only library
add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
# Don't define TINYGLTF_IMPLEMENTATION here - it will be defined in one .cpp file

# Add CLI11 as a header-only library
add_library(CLI11 INTERFACE)
target_include_directories(CLI11 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# Add meshoptimizer as a header-only library
add_library(meshoptimizer INTERFACE)
target_include_directories(meshoptimizer INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# Main executable
add_executable(gltfu 
    src/main.cpp
    src/tinygltf_impl.cpp
    src/gltf_merger.cpp
    src/gltf_merger.h
    src/gltf_dedup.cpp
    src/gltf_dedup.h
    src/gltf_flatten.cpp
    src/gltf_flatten.h
    src/gltf_join.cpp
    src/gltf_join.h
    src/gltf_weld.cpp
    src/gltf_weld.h
    src/gltf_prune.cpp
    src/gltf_prune.h
    src/gltf_simplify.cpp
    src/gltf_simplify.h
    src/gltf_info.cpp
    src/gltf_info.h
    third_party/meshoptimizer_simplifier.cpp
    third_party/meshoptimizer_allocator.cpp
)

target_link_libraries(gltfu PRIVATE 
    CLI11
    tinygltf
    meshoptimizer
)

target_include_directories(gltfu PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compiler warnings
if(MSVC)
    target_compile_options(gltfu PRIVATE /W4)
else()
    target_compile_options(gltfu PRIVATE -Wall -Wextra -pedantic)
endif()

# Installation
install(TARGETS gltfu DESTINATION bin)
