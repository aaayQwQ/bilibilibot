cmake_minimum_required(VERSION 3.21)
project(bilibilibot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network WebSockets)
find_package(ZLIB REQUIRED)

add_executable(bilibilibot
    src/main.cpp
    src/config.h src/config.cpp
    src/bilibili.h src/bilibili.cpp
    src/anthropic.h src/anthropic.cpp
)
target_link_libraries(bilibilibot PRIVATE
    Qt6::Core Qt6::Network Qt6::WebSockets ZLIB::ZLIB
)

if(MSVC)
    target_compile_options(bilibilibot PRIVATE /W4 /utf-8)
else()
    target_compile_options(bilibilibot PRIVATE -Wall -Wextra -Wpedantic)
endif()
