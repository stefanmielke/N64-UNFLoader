cmake_minimum_required(VERSION 3.21)
project(networking_test_pc)

set(CMAKE_CXX_STANDARD 20)

include_directories("/usr/local/include")
link_directories("/usr/local/lib")

add_executable(networking_test_pc main.cpp)

target_link_libraries(networking_test_pc PUBLIC enet)