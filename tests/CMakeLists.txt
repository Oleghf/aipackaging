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

aipackaging_add_test_executable(AIPackaging_SolverTests
  "solver/testgridenvironment.cpp"
  "solver/testgridlearning.cpp"
  "solver/testgridsolver.cpp"
  "solver/testgridio.cpp"
  "solver/testpolygon.cpp"
  "solver/testcontractcorpus.cpp"
  "solver/testcompatibilityheaders.cpp"
)
target_link_libraries(AIPackaging_SolverTests PRIVATE AIPackaging_SolverImpl AIPackaging_Solver nlohmann_json::nlohmann_json)
target_compile_definitions(AIPackaging_SolverTests PRIVATE
  AIPACKAGING_CONTRACT_CORPUS_DIR="${CMAKE_SOURCE_DIR}/tests/contracts"
)
set_tests_properties(AIPackaging_SolverTests PROPERTIES LABELS "solver;contract")

if (BUILD_DESKTOP OR BUILD_LEGACY_TESTS)
  aipackaging_add_test_executable(AIPackaging_AppTests
    "core/app/testpackingcontroller.cpp"
    "core/app/testfigurepoolio.cpp"
    "core/app/testpackingscenesaver.cpp"
    "core/app/testdrawcontroller.cpp"
    "core/app/testfigurescenegeometry.cpp"
    "core/app/testpolygonworkspacecontroller.cpp"
  )
  target_include_directories(AIPackaging_AppTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/core/app"
    "${CMAKE_SOURCE_DIR}/src/core/app/services"
    "${CMAKE_SOURCE_DIR}/src/core/app/services/packing"
    "${CMAKE_SOURCE_DIR}/src/core/app/services/io"
  )
  target_link_libraries(AIPackaging_AppTests PRIVATE
    AIPackaging_App AIPackaging_Math AIPackaging_Domain AIPackaging_Contract AIPackaging_Solver
  )
  set_tests_properties(AIPackaging_AppTests PROPERTIES LABELS "app")

  aipackaging_add_test_executable(AIPackaging_LegacyGeometryTests
    "aipackaging_math/geometry/testpoint2d.cpp"
    "aipackaging_math/geometry/testvector2d.cpp"
    "aipackaging_math/geometry/testline2d.cpp"
    "aipackaging_math/geometry/testcircle2d.cpp"
    "aipackaging_math/geometry/testrect2d.cpp"
    "aipackaging_math/geometry/testboundedcurve2d.cpp"
    "aipackaging_math/algorithms/testalgs.cpp"
    "aipackaging_math/algorithms/testCountour2D.cpp"
    "aipackaging_math/algorithms/testmathutils.cpp"
    "aipackaging_math/algorithms/testdataformat.cpp"
  )
  target_link_libraries(AIPackaging_LegacyGeometryTests PRIVATE
    AIPackaging_Math AIPackaging_Domain AIPackaging_Contract
  )
  set_tests_properties(AIPackaging_LegacyGeometryTests PROPERTIES LABELS "legacy")
endif()

if (BUILD_DESKTOP)
  aipackaging_add_test_executable(AIPackaging_GuiTests
    "gui/testqtadapters.cpp"
    "gui/testpolygonworkspacewidget.cpp"
  )
  target_include_directories(AIPackaging_GuiTests PRIVATE "${CMAKE_SOURCE_DIR}/src/gui")
  target_link_libraries(AIPackaging_GuiTests PRIVATE
    AIPackaging_GUI AIPackaging_App AIPackaging_Solver AIPackaging_Math AIPackaging_Contract
  )
  set_tests_properties(AIPackaging_GuiTests PROPERTIES LABELS "gui")
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
endif()

if (BUILD_DESKTOP OR BUILD_LEGACY_TESTS)
  copy_target_dll(AIPackaging_AppTests AIPackaging_Math)
  copy_target_dll(AIPackaging_LegacyGeometryTests AIPackaging_Math)
endif()
if (BUILD_DESKTOP)
  copy_target_dll(AIPackaging_GuiTests AIPackaging_Math)
  copy_target_qt_dll(AIPackaging_GuiTests)
endif()

aipackaging_assert_target_dependencies(
  TARGET AIPackaging_NestingCoreTests
  ALLOWED_DIRECT AIPackaging_NestingCore
)
aipackaging_assert_target_dependencies(
  TARGET AIPackaging_SolverTests
  ALLOWED_DIRECT AIPackaging_SolverImpl AIPackaging_Solver
)
if (BUILD_DESKTOP OR BUILD_LEGACY_TESTS)
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_AppTests
    ALLOWED_DIRECT AIPackaging_App AIPackaging_Math AIPackaging_Domain AIPackaging_Contract AIPackaging_Solver
  )
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_LegacyGeometryTests
    ALLOWED_DIRECT AIPackaging_Math AIPackaging_Domain AIPackaging_Contract
  )
endif()
if (BUILD_DESKTOP)
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_GuiTests
    ALLOW_QT
    ALLOWED_DIRECT AIPackaging_GUI AIPackaging_App AIPackaging_Solver AIPackaging_Math AIPackaging_Contract
  )
endif()
