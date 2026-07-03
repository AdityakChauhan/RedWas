cmake_minimum_required(VERSION 3.20)

project(RedWas)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(RedWas
    src/main.cpp
    src/server/Server.cpp
    src/resp/RespParser.cpp
    src/serializer/RespSerializer.cpp
    src/dispatch/CommandDispatcher.cpp
    src/store/DataStore.cpp
)

target_include_directories(RedWas PRIVATE
    src
)