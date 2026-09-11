# Подключаем независимое ядро раскроя и текущую реализацию решателя.
add_subdirectory("nesting/")
add_subdirectory("solver/")

if (BUILD_DESKTOP OR BUILD_LEGACY_TESTS)
  add_subdirectory("core/domain/")
  add_subdirectory("core/contract/")
  add_subdirectory("core/app/")
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
  target_link_libraries(${PROJECT_NAME} PRIVATE AIPackaging_App AIPackaging_GUI AIPackaging_Math)

  copy_target_dll(${PROJECT_NAME} AIPackaging_Math)
  copy_target_qt_dll(${PROJECT_NAME})
endif()
