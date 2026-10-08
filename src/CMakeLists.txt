# Подключаем независимое ядро раскроя и текущую реализацию решателя.
add_subdirectory("nesting/")
add_subdirectory("solver/")

if (BUILD_ONNX_BACKEND)
  add_subdirectory("inference/onnx/")
endif()

if (BUILD_DESKTOP OR BUILD_APPLICATION_TESTS)
  add_subdirectory("editor/")
  add_subdirectory("import/dxf/")
  add_subdirectory("persistence/")
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
  target_compile_definitions(${PROJECT_NAME} PRIVATE AIPACKAGING_PROJECT_VERSION="${PROJECT_VERSION}")
  if(WIN32)
    enable_language(RC)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE "$<NOT:$<CONFIG:Debug>>")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(AIPACKAGING_ICON "${CMAKE_CURRENT_BINARY_DIR}/aipackaging.ico")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/release/make_icon.py" "${AIPACKAGING_ICON}"
      COMMAND_ERROR_IS_FATAL ANY)
    configure_file("${CMAKE_SOURCE_DIR}/cmake/windows_version.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/windows_version.rc" @ONLY)
    configure_file("${CMAKE_SOURCE_DIR}/cmake/release_build.json.in" "${CMAKE_BINARY_DIR}/release-build.json" @ONLY)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows_version.rc")
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/DEBUG>)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AIPACKAGING_ICON}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/aipackaging.ico" VERBATIM)
  endif()
  target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(${PROJECT_NAME} PRIVATE
    AIPackaging_Application AIPackaging_DesktopInfrastructure AIPackaging_GUI)

  copy_target_qt_dll(${PROJECT_NAME})
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/docs/user" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/docs/user"
    VERBATIM)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/examples/polygon"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/examples/polygon"
      "$<TARGET_FILE_DIR:${PROJECT_NAME}>/examples/polygon"
    VERBATIM)
  if (BUILD_ONNX_BACKEND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE AIPACKAGING_HAS_ONNX_BACKEND=1)
    aipackaging_copy_onnxruntime(${PROJECT_NAME})
  endif()
endif()
