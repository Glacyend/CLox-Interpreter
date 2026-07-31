cmake_minimum_required(VERSION 3.31)

project(clox)

set(CMAKE_C_STANDARD 23)
set(
    HEADERS
    src/common.h
)
set(
    SOURCES
    src/main.c
)

add_executable(lox ${HEADERS} ${SOURCES})
target_compile_options(lox PUBLIC -O2 -Wall -Wextra)
