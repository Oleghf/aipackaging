cmake_minimum_required(VERSION 3.27)
project("AIPackaging" VERSION 0.10.0 LANGUAGES CXX)

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
include("${CMAKE_SOURCE_DIR}/cmake/architecture_checks.cmake")
include(FetchContent)

option(BUILD_DESKTOP "Build the Qt desktop application" ON)
option(BUILD_SOLVER_CLI "Build the headless grid solver CLI" ON)
option(BUILD_PYTHON_BINDINGS "Build the optional Python ML bindings" OFF)
option(BUILD_ONNX_BACKEND "Build the optional polygon ONNX inference backend" OFF)
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_APPLICATION_TESTS "Build polygon application and infrastructure tests without Qt" OFF)

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

if (BUILD_ONNX_BACKEND)
  set(AIPACKAGING_ONNXRUNTIME_ROOT "" CACHE PATH "Path to an unpacked ONNX Runtime CPU SDK")
  if (NOT AIPACKAGING_ONNXRUNTIME_ROOT)
    if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
      FetchContent_Declare(
        onnxruntime_prebuilt
        URL https://github.com/microsoft/onnxruntime/releases/download/v1.29.0/onnxruntime-win-x64-1.29.0.zip
        URL_HASH SHA256=c9b4b7086b529ad814f428c1bad028e20a25d7dc0699836775faace4ab5b78b2
      )
      FetchContent_MakeAvailable(onnxruntime_prebuilt)
      if (EXISTS "${onnxruntime_prebuilt_SOURCE_DIR}/include/onnxruntime_cxx_api.h")
        set(AIPACKAGING_ONNXRUNTIME_ROOT "${onnxruntime_prebuilt_SOURCE_DIR}")
      elseif (EXISTS "${onnxruntime_prebuilt_SOURCE_DIR}/onnxruntime-win-x64-1.29.0/include/onnxruntime_cxx_api.h")
        set(AIPACKAGING_ONNXRUNTIME_ROOT "${onnxruntime_prebuilt_SOURCE_DIR}/onnxruntime-win-x64-1.29.0")
      endif()
    else()
      message(FATAL_ERROR "BUILD_ONNX_BACKEND requires AIPACKAGING_ONNXRUNTIME_ROOT on this platform")
    endif()
  endif()
  if (NOT EXISTS "${AIPACKAGING_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "AIPACKAGING_ONNXRUNTIME_ROOT does not contain ONNX Runtime headers")
  endif()
  add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
  set_target_properties(onnxruntime::onnxruntime PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${AIPACKAGING_ONNXRUNTIME_ROOT}/include"
  )
  if (WIN32)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
      IMPORTED_IMPLIB "${AIPACKAGING_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
      IMPORTED_LOCATION "${AIPACKAGING_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
    )
  else()
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
      IMPORTED_LOCATION "${AIPACKAGING_ONNXRUNTIME_ROOT}/lib/libonnxruntime.so"
    )
  endif()
endif()

add_subdirectory("src/")
aipackaging_check_production_target_graph()

if (BUILD_TESTS)
    include(CTest)
    add_subdirectory("tests/")
endif()
