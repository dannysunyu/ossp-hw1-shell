cmake_minimum_required(VERSION 3.3)
project(Shell)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")

set(SOURCE_FILES shell.c tokenizer.c tokenizer.h)
add_executable(Shell ${SOURCE_FILES})