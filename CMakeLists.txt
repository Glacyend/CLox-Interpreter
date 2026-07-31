cmake_minimum_required(VERSION 3.31)

project(clox)

set(CMAKE_C_STANDARD 23)
set(
    HEADERS
    src/chunk.h
    src/common.h
    src/debug.h
    src/memory.h
)
set(
    SOURCES
    src/chunk.c
    src/debug.c
    src/main.c
    src/memory.c
    src/value.c
)

add_executable(lox ${HEADERS} ${SOURCES})
target_compile_options(lox PUBLIC -O2 -Wall -Wextra)
