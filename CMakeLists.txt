cmake_minimum_required(VERSION 3.5)

project(nds)

set(CMAKE_BUILD_TYPE DEBUG)

set(SOURCES src/main.cpp
            src/core/bus.cpp
            src/core/arm9/instr_decoding.cpp
            src/core/arm9/arm9.cpp
            src/core/arm9/cp15.cpp)

add_executable(nds ${SOURCES})

target_include_directories(nds PRIVATE ${CMAKE_SOURCE_DIR})

set_property(TARGET nds PROPERTY CXX_STANDARD 23)