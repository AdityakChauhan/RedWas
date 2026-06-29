# REDWAS - RedIs but Was

## CMake file
cmake_minimum_required(VERSION 3.20)

project(RedWas)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(RedWas
    src/main.cpp
)

