cmake_minimum_required(VERSION 3.24)

project(opencraft
    VERSION 0.1.0
    DESCRIPTION "OpenCraft - an original C++20 voxel sandbox"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPENCRAFT_BUILD_TESTS "Build OpenCraft tests" ON)
option(OPENCRAFT_BUILD_CLIENT "Build the opencraft client executable" ON)
option(OPENCRAFT_BUILD_SERVER "Build the opencraft-server executable" OFF)

include(cmake/deps.cmake)

add_subdirectory(engine/core)
add_subdirectory(engine/noise)
add_subdirectory(engine/voxel)
add_subdirectory(engine/render)
add_subdirectory(engine/physics)
add_subdirectory(engine/net)

add_subdirectory(game/common)
add_subdirectory(game/server)
if(OPENCRAFT_BUILD_CLIENT)
    add_subdirectory(game/client)
endif()

if(OPENCRAFT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
