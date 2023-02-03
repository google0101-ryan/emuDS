cmake_minimum_required(VERSION 3.5)

project(nds)

set(SOURCES src/main.cpp
            src/core/bus.cpp
            src/core/arm9/instr_decoding.cpp
            src/core/arm9/arm9.cpp)

add_executable(nds ${SOURCES})

target_include_directories(nds PRIVATE ${CMAKE_SOURCE_DIR})