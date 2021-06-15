cmake_minimum_required(VERSION 3.16)
project(cshogi)

set(CMAKE_CXX_STANDARD 14)

file(
        GLOB CSHOGI_BASE
        src/*.cpp
        src/*.hpp
)
add_library(cshogi STATIC ${CSHOGI_BASE})
target_include_directories(cshogi PUBLIC .)

add_executable(
        generator
        test/generate_test.cpp
)
target_link_libraries(generator_main PUBLIC YaneuraOu)