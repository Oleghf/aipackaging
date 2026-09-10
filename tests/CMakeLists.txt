FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.16.0
)

FetchContent_MakeAvailable(googletest)

add_executable(AIPackaging_Tests
"testmain.cpp"
"core/app/testpackingcontroller.cpp"
"core/app/testfigurepoolio.cpp"
"core/app/testpackingscenesaver.cpp"
"core/app/testdrawcontroller.cpp"
"core/app/testfigurescenegeometry.cpp"
"core/app/testpolygonworkspacecontroller.cpp"
"solver/testgridenvironment.cpp"
"solver/testgridlearning.cpp"
"solver/testgridsolver.cpp"
"solver/testgridio.cpp"
"solver/testpolygon.cpp"
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

target_include_directories(AIPackaging_Tests PUBLIC
"${CMAKE_SOURCE_DIR}/tests"
"${CMAKE_SOURCE_DIR}/src/core/app"
"${CMAKE_SOURCE_DIR}/src/core/app/services"
"${CMAKE_SOURCE_DIR}/src/core/app/services/packing"
"${CMAKE_SOURCE_DIR}/src/core/app/services/io"
)

target_link_libraries(AIPackaging_Tests PUBLIC
gtest
gtest_main
AIPackaging_App
AIPackaging_Math
AIPackaging_Domain
AIPackaging_Contract
AIPackaging_Solver
)

if (BUILD_DESKTOP)
  target_sources(AIPackaging_Tests PRIVATE "gui/testqtadapters.cpp" "gui/testpolygonworkspacewidget.cpp")
  target_include_directories(AIPackaging_Tests PUBLIC "${CMAKE_SOURCE_DIR}/src/gui")
  target_link_libraries(AIPackaging_Tests PUBLIC AIPackaging_GUI)
endif()

add_test(NAME AllTests COMMAND $<TARGET_FILE:AIPackaging_Tests>)

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
      add_test(
        NAME "${validate_test}"
        COMMAND $<TARGET_FILE:AIPackaging_Cli> validate
          --problem "${CMAKE_SOURCE_DIR}/examples/polygon/problem-${polygon_case}.json"
          --solution "${solution_file}"
      )
      set_tests_properties("${validate_test}" PROPERTIES DEPENDS "${solve_test}")
    endforeach()
  endforeach()
endif()

copy_target_dll(AIPackaging_Tests AIPackaging_Math)
if (BUILD_DESKTOP)
  copy_target_qt_dll(AIPackaging_Tests)
endif()
