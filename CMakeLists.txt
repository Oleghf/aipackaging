cmake_minimum_required(VERSION 3.27)
project("AIPackaging" VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()

include("${CMAKE_SOURCE_DIR}/cmake/helpers.cmake")
include(FetchContent)

FetchContent_Declare(
  AIPackaging_Math
  GIT_REPOSITORY https://gitverse.ru/polytech_kolomna/aipackaging_math.git
  GIT_TAG        9d0a95b8f8fa7dccfddfcc6eabcda1f664705d22
)

FetchContent_MakeAvailable(AIPackaging_Math)

option(BUILD_TESTS "Build tests" OFF)

add_subdirectory("src/")

if (BUILD_TESTS)
    include(CTest)
    add_subdirectory("tests/")
endif()
