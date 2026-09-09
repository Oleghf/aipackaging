cmake_minimum_required(VERSION 3.27)
project("AIPackaging" VERSION 0.1 LANGUAGES CXX)

set(AIPACKAGING_BUILD_REVISION "unknown")
find_package(Git QUIET)
if (Git_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE AIPACKAGING_BUILD_REVISION
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
endif()

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

set(JSON_BuildTests OFF CACHE INTERNAL "Disable nlohmann/json tests")
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
)
FetchContent_MakeAvailable(nlohmann_json)

option(BUILD_DESKTOP "Build the Qt desktop application" ON)
option(BUILD_SOLVER_CLI "Build the headless grid solver CLI" ON)
option(BUILD_TESTS "Build tests" OFF)

add_subdirectory("src/")

if (BUILD_TESTS)
    include(CTest)
    add_subdirectory("tests/")
endif()
