cmake_minimum_required(VERSION 3.10)
project(time-announce)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(yaml-cpp REQUIRED)

add_executable(time-announce time_announce.cpp)
target_link_libraries(time-announce yaml-cpp)
