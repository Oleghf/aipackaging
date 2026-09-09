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
"gui/testqtadapters.cpp"
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
"${CMAKE_SOURCE_DIR}/src/gui"
)

target_link_libraries(AIPackaging_Tests PUBLIC
gtest
gtest_main
AIPackaging_App
AIPackaging_Math
AIPackaging_Domain
AIPackaging_Contract
AIPackaging_GUI
)

add_test(NAME AllTests COMMAND $<TARGET_FILE:AIPackaging_Tests>)

copy_target_dll(AIPackaging_Tests AIPackaging_Math)
copy_target_qt_dll(AIPackaging_Tests)
