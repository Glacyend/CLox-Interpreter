cmake_minimum_required(VERSION 3.31)

project(clox)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 23)
set(
    HEADERS
    src/chunk.h
    src/common.h
    src/compiler.h
    src/debug.h
    src/memory.h
    src/scanner.h
    src/value.h
    src/vm.h
)
set(
    SOURCES
    src/chunk.c
    src/compiler.c
    src/debug.c
    src/main.c
    src/memory.c
    src/scanner.c
    src/value.c
    src/vm.c
)

add_executable(lox ${HEADERS} ${SOURCES})
target_compile_options(lox PUBLIC -O2 -Wall -Wextra)
