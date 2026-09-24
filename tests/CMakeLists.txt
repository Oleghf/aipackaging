FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.16.0
)

FetchContent_MakeAvailable(googletest)

function(aipackaging_add_test_executable target_name)
  add_executable(${target_name} "testmain.cpp" ${ARGN})
  target_include_directories(${target_name} PRIVATE "${CMAKE_SOURCE_DIR}/tests")
  target_link_libraries(${target_name} PRIVATE gtest)
  add_test(NAME ${target_name} COMMAND $<TARGET_FILE:${target_name}>)
endfunction()

aipackaging_add_test_executable(AIPackaging_NestingCoreTests
  "nesting/testcommoncontracts.cpp"
)
target_link_libraries(AIPackaging_NestingCoreTests PRIVATE AIPackaging_NestingCore)
set_tests_properties(AIPackaging_NestingCoreTests PROPERTIES LABELS "contract;nesting")

aipackaging_add_test_executable(AIPackaging_GridCoreTests
  "solver/testgridenvironment.cpp"
)
target_link_libraries(AIPackaging_GridCoreTests PRIVATE AIPackaging_GridCore)
set_tests_properties(AIPackaging_GridCoreTests PROPERTIES LABELS "solver;environment;grid")

aipackaging_add_test_executable(AIPackaging_PolygonCoreTests
  "solver/testpolygonenvironment.cpp"
)
target_link_libraries(AIPackaging_PolygonCoreTests PRIVATE AIPackaging_PolygonCore)
set_tests_properties(AIPackaging_PolygonCoreTests PROPERTIES LABELS "solver;environment;polygon")

aipackaging_add_test_executable(AIPackaging_SearchTests
  "solver/testgridsolver.cpp"
  "solver/testpolygonsearch.cpp"
  "solver/testsearchruntime.cpp"
)
target_include_directories(AIPackaging_SearchTests PRIVATE "${CMAKE_SOURCE_DIR}/src/solver/search")
target_link_libraries(AIPackaging_SearchTests PRIVATE AIPackaging_Search AIPackaging_Json)
set_tests_properties(AIPackaging_SearchTests PROPERTIES LABELS "solver;search")

aipackaging_add_test_executable(AIPackaging_JsonTests
  "solver/testatomicfile.cpp"
  "solver/testgridio.cpp"
  "solver/testpolygonio.cpp"
  "solver/testcontractcorpus.cpp"
)
target_link_libraries(AIPackaging_JsonTests PRIVATE AIPackaging_Json AIPackaging_Search nlohmann_json::nlohmann_json)
target_include_directories(AIPackaging_JsonTests PRIVATE "${CMAKE_SOURCE_DIR}/src/solver/json")
target_compile_definitions(AIPackaging_JsonTests PRIVATE
  AIPACKAGING_CONTRACT_CORPUS_DIR="${CMAKE_SOURCE_DIR}/tests/contracts"
)
set_tests_properties(AIPackaging_JsonTests PROPERTIES LABELS "solver;contract;io")

aipackaging_add_test_executable(AIPackaging_LearningTests
  "solver/testgridlearning.cpp"
  "solver/testpolygonlearning.cpp"
)
target_link_libraries(AIPackaging_LearningTests PRIVATE AIPackaging_Learning AIPackaging_Json AIPackaging_Search)
target_include_directories(AIPackaging_LearningTests PRIVATE "${CMAKE_SOURCE_DIR}/src/solver/learning")
set_tests_properties(AIPackaging_LearningTests PROPERTIES LABELS "solver;learning")

aipackaging_add_test_executable(AIPackaging_SolverCompatibilityTests
  "solver/testcompatibilityheaders.cpp"
)
target_link_libraries(AIPackaging_SolverCompatibilityTests PRIVATE AIPackaging_SolverImpl AIPackaging_Solver)
set_tests_properties(AIPackaging_SolverCompatibilityTests PROPERTIES LABELS "solver;contract;compatibility")

if (BUILD_ONNX_BACKEND)
  aipackaging_add_test_executable(AIPackaging_OnnxInferenceTests
    "inference/testpolygononnxinference.cpp"
  )
  target_link_libraries(AIPackaging_OnnxInferenceTests PRIVATE
    AIPackaging_OnnxInference AIPackaging_Json AIPackaging_PolygonCore
  )
  target_compile_definitions(AIPackaging_OnnxInferenceTests PRIVATE
    AIPACKAGING_ONNX_TEST_MODEL="${CMAKE_SOURCE_DIR}/tests/fixtures/models/polygon-policy-smoke"
    AIPACKAGING_ONNX_TEST_PROBLEM="${CMAKE_SOURCE_DIR}/python/tests/fixtures/polygon-smoke-problem.json"
  )
  set_tests_properties(AIPackaging_OnnxInferenceTests PROPERTIES LABELS "solver;inference;onnx;polygon")
  aipackaging_copy_onnxruntime(AIPackaging_OnnxInferenceTests)
endif()

if (BUILD_DESKTOP OR BUILD_APPLICATION_TESTS)
  aipackaging_add_test_executable(AIPackaging_ApplicationTests
    "core/app/testpolygonworkspacecontroller.cpp"
  )
  target_link_libraries(AIPackaging_ApplicationTests PRIVATE AIPackaging_Application)
  set_tests_properties(AIPackaging_ApplicationTests PROPERTIES LABELS "app;application")

  aipackaging_add_test_executable(AIPackaging_DesktopInfrastructureTests
    "infrastructure/testpolygondesktopinfrastructure.cpp"
  )
  target_link_libraries(AIPackaging_DesktopInfrastructureTests PRIVATE
    AIPackaging_DesktopInfrastructure AIPackaging_Search AIPackaging_Json AIPackaging_PolygonCore
  )
  if (BUILD_ONNX_BACKEND)
    target_compile_definitions(AIPackaging_DesktopInfrastructureTests PRIVATE
      AIPACKAGING_ONNX_TEST_MODEL="${CMAKE_SOURCE_DIR}/tests/fixtures/models/polygon-policy-smoke"
    )
    aipackaging_copy_onnxruntime(AIPackaging_DesktopInfrastructureTests)
  endif()
  set_tests_properties(AIPackaging_DesktopInfrastructureTests PROPERTIES LABELS "app;infrastructure;polygon")

endif()

if (BUILD_DESKTOP)
  aipackaging_add_test_executable(AIPackaging_GuiTests
    "gui/testpolygonworkspacewidget.cpp" "gui/testpolygonmainwindow.cpp"
  )
  target_include_directories(AIPackaging_GuiTests PRIVATE "${CMAKE_SOURCE_DIR}/src/gui")
  target_link_libraries(AIPackaging_GuiTests PRIVATE AIPackaging_GUI AIPackaging_Application)
  set_tests_properties(AIPackaging_GuiTests PROPERTIES LABELS "gui")
  foreach(scale IN ITEMS 1 1.5 2)
    string(REPLACE "." "_" scale_name "${scale}")
    add_test(NAME "AIPackaging_GuiScale_${scale_name}"
      COMMAND $<TARGET_FILE:AIPackaging_GuiTests>
        --gtest_filter=PolygonMainWindow.FitsMinimumResolutionAndExposesAccessibleControls)
    set_tests_properties("AIPackaging_GuiScale_${scale_name}" PROPERTIES
      LABELS "gui;scale"
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${scale}")
  endforeach()
endif()

if (BUILD_SOLVER_CLI)
  foreach(solver_name IN ITEMS input-first-fit area-left-bottom max-side-left-bottom random-left-bottom beam)
    add_test(
      NAME "GridCli_${solver_name}"
      COMMAND $<TARGET_FILE:AIPackaging_Cli> solve
        --input "${CMAKE_SOURCE_DIR}/examples/grid/problem-small.json"
        --output "$<TARGET_FILE_DIR:AIPackaging_Cli>/solution-${solver_name}.json"
        --solver "${solver_name}"
        --random-iterations 8
        --beam-width 8
        --max-expanded-states 5000
        --timeout-ms 0
    )
    set_tests_properties("GridCli_${solver_name}" PROPERTIES LABELS "cli;grid")
  endforeach()

  add_test(
    NAME GridCli_PartialExitCode
    COMMAND ${CMAKE_COMMAND}
      "-DCLI=$<TARGET_FILE:AIPackaging_Cli>"
      "-DINPUT=${CMAKE_SOURCE_DIR}/examples/grid/problem-partial.json"
      "-DOUTPUT=$<TARGET_FILE_DIR:AIPackaging_Cli>/solution-partial.json"
      "-DSOLVER=input-first-fit"
      "-DEXPECTED_EXIT=2"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/assert_cli_exit.cmake"
  )
  set_tests_properties(GridCli_PartialExitCode PROPERTIES LABELS "cli;grid")

  add_test(
    NAME GridCli_InvalidJsonExitCode
    COMMAND ${CMAKE_COMMAND}
      "-DCLI=$<TARGET_FILE:AIPackaging_Cli>"
      "-DINPUT=${CMAKE_SOURCE_DIR}/examples/grid/problem-invalid.json"
      "-DOUTPUT=$<TARGET_FILE_DIR:AIPackaging_Cli>/solution-invalid.json"
      "-DSOLVER=area-left-bottom"
      "-DEXPECTED_EXIT=3"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/assert_cli_exit.cmake"
  )
  set_tests_properties(GridCli_InvalidJsonExitCode PROPERTIES LABELS "cli;grid")

  foreach(polygon_case IN ITEMS small concave curves-and-hole)
    foreach(solver_name IN ITEMS input-first-fit area-left-bottom max-side-left-bottom random-left-bottom beam)
      set(solve_test "PolygonCli_Solve_${polygon_case}_${solver_name}")
      set(validate_test "PolygonCli_Validate_${polygon_case}_${solver_name}")
      set(solution_file "$<TARGET_FILE_DIR:AIPackaging_Cli>/polygon-${polygon_case}-${solver_name}.json")
      add_test(
        NAME "${solve_test}"
        COMMAND $<TARGET_FILE:AIPackaging_Cli> solve
          --input "${CMAKE_SOURCE_DIR}/examples/polygon/problem-${polygon_case}.json"
          --output "${solution_file}"
          --solver "${solver_name}"
          --random-iterations 8
          --beam-width 8
          --max-expanded-states 5000
          --timeout-ms 0
      )
      set_tests_properties("${solve_test}" PROPERTIES LABELS "cli;polygon")
      add_test(
        NAME "${validate_test}"
        COMMAND $<TARGET_FILE:AIPackaging_Cli> validate
          --problem "${CMAKE_SOURCE_DIR}/examples/polygon/problem-${polygon_case}.json"
          --solution "${solution_file}"
      )
      set_tests_properties("${validate_test}" PROPERTIES DEPENDS "${solve_test}" LABELS "cli;polygon")
    endforeach()
  endforeach()

  if (BUILD_ONNX_BACKEND)
    add_test(
      NAME GridCli_RejectsNeuralSolver
      COMMAND ${CMAKE_COMMAND}
        "-DCLI=$<TARGET_FILE:AIPackaging_Cli>"
        "-DINPUT=${CMAKE_SOURCE_DIR}/examples/grid/problem-small.json"
        "-DOUTPUT=$<TARGET_FILE_DIR:AIPackaging_Cli>/grid-neural-rejected.json"
        "-DSOLVER=neural-greedy"
        "-DEXPECTED_EXIT=1"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/assert_cli_exit.cmake"
    )
    set_tests_properties(GridCli_RejectsNeuralSolver PROPERTIES LABELS "cli;onnx;grid")
    add_test(
      NAME PolygonCli_ValidateModel
      COMMAND $<TARGET_FILE:AIPackaging_Cli> validate-model
        --input "${CMAKE_SOURCE_DIR}/tests/fixtures/models/polygon-policy-smoke"
    )
    set_tests_properties(PolygonCli_ValidateModel PROPERTIES LABELS "cli;onnx;polygon")
    set(neural_solution "$<TARGET_FILE_DIR:AIPackaging_Cli>/polygon-neural-smoke.json")
    add_test(
      NAME PolygonCli_SolveNeural
      COMMAND $<TARGET_FILE:AIPackaging_Cli> solve
        --input "${CMAKE_SOURCE_DIR}/python/tests/fixtures/polygon-smoke-problem.json"
        --output "${neural_solution}"
        --solver neural-greedy
        --model "${CMAKE_SOURCE_DIR}/tests/fixtures/models/polygon-policy-smoke"
        --timeout-ms 0
    )
    set_tests_properties(PolygonCli_SolveNeural PROPERTIES LABELS "cli;onnx;polygon")
    add_test(
      NAME PolygonCli_ValidateNeural
      COMMAND $<TARGET_FILE:AIPackaging_Cli> validate
        --problem "${CMAKE_SOURCE_DIR}/python/tests/fixtures/polygon-smoke-problem.json"
        --solution "${neural_solution}"
    )
    set_tests_properties(PolygonCli_ValidateNeural PROPERTIES
      DEPENDS PolygonCli_SolveNeural
      LABELS "cli;onnx;polygon"
    )
  endif()
endif()

if (BUILD_DESKTOP)
  copy_target_qt_dll(AIPackaging_GuiTests)
  find_package(Qt6QOffscreenIntegrationPlugin CONFIG REQUIRED
    PATHS "${Qt6_DIR}/../Qt6Gui" NO_DEFAULT_PATH)
  add_custom_command(TARGET AIPackaging_GuiTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AIPackaging_GuiTests>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
      "$<TARGET_FILE_DIR:AIPackaging_GuiTests>/platforms"
    VERBATIM)
  add_custom_command(TARGET AIPackaging_GuiTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AIPackaging_GuiTests>/examples/polygon"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/examples/polygon"
      "$<TARGET_FILE_DIR:AIPackaging_GuiTests>/examples/polygon"
    VERBATIM)
endif()

aipackaging_assert_target_dependencies(
  TARGET AIPackaging_NestingCoreTests
  ALLOWED_DIRECT AIPackaging_NestingCore
)
aipackaging_assert_target_dependencies(
  TARGET AIPackaging_GridCoreTests
  ALLOWED_DIRECT AIPackaging_GridCore
)
aipackaging_assert_target_dependencies(
  TARGET AIPackaging_PolygonCoreTests
  ALLOWED_DIRECT AIPackaging_PolygonCore
)
aipackaging_assert_target_dependencies(
  TARGET AIPackaging_SearchTests
  ALLOWED_DIRECT AIPackaging_Search AIPackaging_Json
)
aipackaging_assert_target_dependencies(
  TARGET AIPackaging_JsonTests
  ALLOWED_DIRECT AIPackaging_Json AIPackaging_Search
)
aipackaging_assert_target_dependencies(
  TARGET AIPackaging_LearningTests
  ALLOWED_DIRECT AIPackaging_Learning AIPackaging_Json AIPackaging_Search
)
aipackaging_assert_target_dependencies(
  TARGET AIPackaging_SolverCompatibilityTests
  ALLOWED_DIRECT AIPackaging_SolverImpl AIPackaging_Solver
)
if (BUILD_DESKTOP OR BUILD_APPLICATION_TESTS)
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_ApplicationTests
    ALLOWED_DIRECT AIPackaging_Application
  )
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_DesktopInfrastructureTests
    ALLOWED_DIRECT AIPackaging_DesktopInfrastructure AIPackaging_Search AIPackaging_Json AIPackaging_PolygonCore
  )
endif()
if (BUILD_ONNX_BACKEND)
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_OnnxInferenceTests
    ALLOWED_DIRECT AIPackaging_OnnxInference AIPackaging_Json AIPackaging_PolygonCore
  )
endif()
if (BUILD_DESKTOP)
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_GuiTests
    ALLOW_QT
    ALLOWED_DIRECT AIPackaging_GUI AIPackaging_Application
  )
endif()
