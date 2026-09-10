cmake_minimum_required(VERSION 3.27)
project("AIPackaging" VERSION 0.4.0 LANGUAGES CXX)

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
set(CLIPPER2_TESTS OFF CACHE BOOL "Disable Clipper2 tests" FORCE)
set(CLIPPER2_EXAMPLES OFF CACHE BOOL "Disable Clipper2 examples" FORCE)
set(CLIPPER2_UTILS OFF CACHE BOOL "Disable Clipper2 utilities" FORCE)
set(CLIPPER2_USINGZ OFF CACHE STRING "Build only the ordinary Clipper2 target" FORCE)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  Clipper2
  GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git
  GIT_TAG        21ebba05db8894f0c7217ad35ea518080f324946
  SOURCE_SUBDIR  CPP
)
FetchContent_MakeAvailable(Clipper2)

option(BUILD_DESKTOP "Build the Qt desktop application" ON)
option(BUILD_SOLVER_CLI "Build the headless grid solver CLI" ON)
option(BUILD_PYTHON_BINDINGS "Build the optional Python ML bindings" OFF)
option(BUILD_TESTS "Build tests" OFF)

add_subdirectory("src/")

if (BUILD_TESTS)
    include(CTest)
    add_subdirectory("tests/")
endif()
