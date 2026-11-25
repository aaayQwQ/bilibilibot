cmake_minimum_required(VERSION 3.21)
project(bilibilibot VERSION 1.0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
include(CTest)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network WebSockets)

add_executable(bilibilibot
    src/main.cpp
    src/config.hpp src/config.cpp
    src/bilibili.hpp src/bilibili.cpp
    src/anthropic.hpp src/anthropic.cpp
    src/plugin_api.hpp src/plugin_api.cpp
)
target_link_libraries(bilibilibot PRIVATE Qt6::Core Qt6::Network Qt6::WebSockets)
if(MSVC)
    target_compile_options(bilibilibot PRIVATE /W4 /utf-8)
else()
    target_compile_options(bilibilibot PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    add_executable(test_plugin_api tests/test_plugin_api.cpp src/plugin_api.hpp src/plugin_api.cpp)
    target_include_directories(test_plugin_api PRIVATE src)
    target_link_libraries(test_plugin_api PRIVATE Qt6::Core Qt6::Test)
    if(MSVC)
        target_compile_options(test_plugin_api PRIVATE /W4 /utf-8)
    else()
        target_compile_options(test_plugin_api PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME plugin_api COMMAND test_plugin_api)

    add_executable(test_config tests/test_config.cpp src/config.hpp src/config.cpp)
    target_include_directories(test_config PRIVATE src)
    target_link_libraries(test_config PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    if(MSVC)
        target_compile_options(test_config PRIVATE /W4 /utf-8)
    else()
        target_compile_options(test_config PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME config COMMAND test_config)
endif()
