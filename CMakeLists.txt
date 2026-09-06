cmake_minimum_required(VERSION 3.20)

project(tsmom_engine
    VERSION 0.1.0
    DESCRIPTION "Event-driven backtesting engine with a time-series momentum strategy"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Warning set applied to every target we own, but never to fetched dependencies.
add_library(backtest_warnings INTERFACE)
target_compile_options(backtest_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

option(TSMOM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
if(TSMOM_WARNINGS_AS_ERRORS)
    target_compile_options(backtest_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>)
endif()

add_subdirectory(src)
add_subdirectory(strategies)
add_subdirectory(apps)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
