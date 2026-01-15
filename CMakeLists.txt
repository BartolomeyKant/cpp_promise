cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(cpp_promise)

list(APPEND srcs src/main.cpp)

add_executable(main ${srcs})
