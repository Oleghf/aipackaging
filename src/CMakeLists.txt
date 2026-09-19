# Подключаем независимое ядро раскроя и текущую реализацию решателя.
add_subdirectory("nesting/")
add_subdirectory("solver/")

if (BUILD_ONNX_BACKEND)
  add_subdirectory("inference/onnx/")
endif()

if ((BUILD_DESKTOP AND BUILD_LEGACY_DESKTOP) OR BUILD_LEGACY_TESTS)
  add_subdirectory("core/domain/")
  add_subdirectory("core/contract/")
endif()

if (BUILD_DESKTOP OR BUILD_LEGACY_TESTS)
  add_subdirectory("core/app/")
  add_subdirectory("infrastructure/")
endif()

if (BUILD_SOLVER_CLI)
  add_subdirectory("cli/")
endif()

if (BUILD_PYTHON_BINDINGS)
  add_subdirectory("python/")
endif()

if (BUILD_DESKTOP)
  set(CMAKE_AUTOMOC ON)
  set(CMAKE_AUTOUIC ON)
  set(CMAKE_AUTORCC ON)

  find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
  add_subdirectory("gui/")

  add_executable(${PROJECT_NAME} "main.cpp")
  target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(${PROJECT_NAME} PRIVATE
    AIPackaging_Application AIPackaging_DesktopInfrastructure AIPackaging_GUI)

  if (BUILD_LEGACY_DESKTOP)
    target_link_libraries(${PROJECT_NAME} PRIVATE AIPackaging_App AIPackaging_Math)
    target_compile_definitions(${PROJECT_NAME} PRIVATE AIPACKAGING_LEGACY_DESKTOP=1)
    copy_target_dll(${PROJECT_NAME} AIPackaging_Math)
  endif()

  copy_target_qt_dll(${PROJECT_NAME})
  if (BUILD_ONNX_BACKEND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE AIPACKAGING_HAS_ONNX_BACKEND=1)
    aipackaging_copy_onnxruntime(${PROJECT_NAME})
  endif()
endif()
