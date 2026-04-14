cmake_minimum_required(VERSION 3.16)
project(docksmith LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(docksmith
    cmd/main.cpp
    engine/build.cpp
    engine/runtime.cpp
    engine/cache.cpp
    engine/layers.cpp
    engine/parser.cpp
    store/images.cpp
    store/layers.cpp
    store/cache.cpp
    util/tar.cpp
    util/hash.cpp
    util/fs.cpp
)

target_include_directories(docksmith PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(docksmith_integration_phases
    tests/integration_phases.cpp
    engine/runtime.cpp
    engine/parser.cpp
    store/images.cpp
    store/layers.cpp
    util/tar.cpp
    util/hash.cpp
)

target_include_directories(docksmith_integration_phases PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
