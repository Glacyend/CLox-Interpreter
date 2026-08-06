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
    src/object.h
    src/scanner.h
    src/table.h
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
    src/object.c
    src/scanner.c
    src/table.c
    src/value.c
    src/vm.c
)

add_executable(lox ${HEADERS} ${SOURCES})
target_compile_options(lox PUBLIC -O2 -Wall -Wextra)

add_executable(lox-debug ${HEADERS} ${SOURCES})
target_compile_options(lox-debug PUBLIC -g -Wall -Wextra -fsanitize=address,bounds,undefined -DDEBUG_PRINT_CODE -DDEBUG_TRACE_EXECUTION -DDEBUG_STRESS_GC -DDEBUG_LOG_GC)
target_link_options(lox-debug PUBLIC -fsanitize=address,bounds,undefined)
