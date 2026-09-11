add_library(AIPackaging_SolverImpl STATIC)

target_sources(AIPackaging_SolverImpl PRIVATE
  "gridenvironment.cpp"
  "gridio.cpp"
  "gridlearning.cpp"
  "gridsolver.cpp"
  "polygonenvironment.cpp"
  "polygonio.cpp"
  "polygonlearning.cpp"
  "polygonsolver.cpp"
)
target_sources(AIPackaging_SolverImpl PUBLIC
  FILE_SET HEADERS
  BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
  FILES
    "include/aipackaging/nesting/grid_environment.h"
    "include/aipackaging/nesting/grid_io.h"
    "include/aipackaging/nesting/grid_learning.h"
    "include/aipackaging/nesting/grid_solver.h"
    "include/aipackaging/nesting/grid_types.h"
    "include/aipackaging/nesting/polygon_environment.h"
    "include/aipackaging/nesting/polygon_io.h"
    "include/aipackaging/nesting/polygon_learning.h"
    "include/aipackaging/nesting/polygon_solver.h"
    "include/aipackaging/nesting/polygon_types.h"
    "include/aipackaging/nesting/search_contracts.h"
)

target_include_directories(AIPackaging_SolverImpl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(AIPackaging_SolverImpl
  PUBLIC AIPackaging_NestingCore
  PRIVATE nlohmann_json::nlohmann_json Clipper2
)
set_target_properties(AIPackaging_SolverImpl PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(AIPackaging_SolverImpl PRIVATE
  AIPACKAGING_PROJECT_VERSION="${PROJECT_VERSION}"
  AIPACKAGING_BUILD_REVISION="${AIPACKAGING_BUILD_REVISION}"
)

# Старое имя target и плоские заголовки сохраняются до миграции App/GUI в A6.
add_library(AIPackaging_Solver INTERFACE)
target_include_directories(AIPackaging_Solver INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(AIPackaging_Solver INTERFACE AIPackaging_SolverImpl)
