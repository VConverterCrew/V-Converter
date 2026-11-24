cmake_minimum_required(VERSION 3.16)

project(VBanana
    VERSION 1.0.0
    DESCRIPTION "GameBanana API framework for Friday Night Funkin' mods: requests, parsing, engine detection, file picking and archive inspection"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VBANANA_BUILD_TESTS "Build tests" OFF)

add_library(VBanana STATIC
    src/Json.cpp
    src/Banana.cpp
    include/vbanana/VBanana.h
)
add_library(VBanana::VBanana ALIAS VBanana)

target_include_directories(VBanana PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(VBanana PROPERTIES OUTPUT_NAME "vbanana" POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(VBanana PRIVATE /O2 /utf-8 /EHsc)
else()
    target_compile_options(VBanana PRIVATE -O2 -Wall -Wextra)
endif()

if(VBANANA_BUILD_TESTS)
    enable_testing()
    add_executable(vbanana_tests tests/test_banana.cpp)
    target_link_libraries(vbanana_tests PRIVATE VBanana::VBanana)
    target_include_directories(vbanana_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME VBananaTests COMMAND vbanana_tests)
endif()
