cmake_minimum_required(VERSION 3.16)
project(c_shogi)

set(CMAKE_CXX_STANDARD 14)

file(
        GLOB CSHOGI_BASE
        src/*.cpp
        src/*.hpp
)
add_library(c_shogi STATIC ${CSHOGI_BASE})
target_include_directories(c_shogi PUBLIC .)

add_executable(
        test
        test/test.cpp
)
target_link_libraries(generator_main PUBLIC c_shogi)

add_executable(
        generator
        test/generate_test.cpp
)
target_link_libraries(generator_main PUBLIC c_shogi)
