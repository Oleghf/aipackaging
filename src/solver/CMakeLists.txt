set(AIPACKAGING_NESTING_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

function(aipackaging_configure_nesting_library target)
  target_include_directories(${target} PUBLIC "${AIPACKAGING_NESTING_INCLUDE_DIR}")
  target_link_libraries(${target} PUBLIC AIPackaging_NestingCore)
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

add_library(AIPackaging_SearchContracts STATIC "search/contracts/searchcontracts.cpp")
aipackaging_configure_nesting_library(AIPackaging_SearchContracts)
target_sources(AIPackaging_SearchContracts PUBLIC
  FILE_SET HEADERS
  BASE_DIRS "${AIPACKAGING_NESTING_INCLUDE_DIR}"
  FILES "include/aipackaging/nesting/search_contracts.h"
)

add_library(AIPackaging_GridCore STATIC "grid/gridenvironment.cpp")
aipackaging_configure_nesting_library(AIPackaging_GridCore)
target_sources(AIPackaging_GridCore PUBLIC
  FILE_SET HEADERS
  BASE_DIRS "${AIPACKAGING_NESTING_INCLUDE_DIR}"
  FILES
    "include/aipackaging/nesting/grid_environment.h"
    "include/aipackaging/nesting/grid_types.h"
)

add_library(AIPackaging_PolygonCore STATIC
  "polygon/polygoncollision.cpp"
  "polygon/polygonenvironment.cpp"
  "polygon/polygonnormalization.cpp"
  "polygon/polygoncandidates.cpp"
  "polygon/polygonobjective.cpp"
  "polygon/polygonvalidation.cpp"
)
aipackaging_configure_nesting_library(AIPackaging_PolygonCore)
target_link_libraries(AIPackaging_PolygonCore PRIVATE Clipper2)
target_sources(AIPackaging_PolygonCore PUBLIC
  FILE_SET HEADERS
  BASE_DIRS "${AIPACKAGING_NESTING_INCLUDE_DIR}"
  FILES
    "include/aipackaging/nesting/polygon_environment.h"
    "include/aipackaging/nesting/polygon_types.h"
)

add_library(AIPackaging_Search STATIC
  "search/gridsolver.cpp"
  "search/polygonsolver.cpp"
  "search/searchruntime.cpp"
)
target_include_directories(AIPackaging_Search PUBLIC "${AIPACKAGING_NESTING_INCLUDE_DIR}")
target_link_libraries(AIPackaging_Search PUBLIC
  AIPackaging_SearchContracts AIPackaging_GridCore AIPackaging_PolygonCore
)
set_target_properties(AIPackaging_Search PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_sources(AIPackaging_Search PUBLIC
  FILE_SET HEADERS
  BASE_DIRS "${AIPACKAGING_NESTING_INCLUDE_DIR}"
  FILES
    "include/aipackaging/nesting/grid_solver.h"
    "include/aipackaging/nesting/polygon_solver.h"
)
target_compile_definitions(AIPackaging_Search PRIVATE
  AIPACKAGING_PROJECT_VERSION="${PROJECT_VERSION}"
  AIPACKAGING_BUILD_REVISION="${AIPACKAGING_BUILD_REVISION}"
)

add_library(AIPackaging_StrictJsonSupport STATIC
  "json/atomicfile.cpp"
  "json/strictjson.cpp"
)
target_include_directories(AIPackaging_StrictJsonSupport PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/json")
target_link_libraries(AIPackaging_StrictJsonSupport PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(AIPackaging_StrictJsonSupport PUBLIC cxx_std_20)

add_library(AIPackaging_Json STATIC
  "json/gridio.cpp"
  "json/polygonio.cpp"
  "json/solvermetadatavalidation.cpp"
)
target_include_directories(AIPackaging_Json PUBLIC "${AIPACKAGING_NESTING_INCLUDE_DIR}")
target_link_libraries(AIPackaging_Json
  PUBLIC AIPackaging_GridCore AIPackaging_PolygonCore
  PRIVATE AIPackaging_SearchContracts AIPackaging_StrictJsonSupport nlohmann_json::nlohmann_json
)
set_target_properties(AIPackaging_Json PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_sources(AIPackaging_Json PUBLIC
  FILE_SET HEADERS
  BASE_DIRS "${AIPACKAGING_NESTING_INCLUDE_DIR}"
  FILES
    "include/aipackaging/nesting/grid_io.h"
    "include/aipackaging/nesting/polygon_io.h"
)

add_library(AIPackaging_Learning STATIC "learning/gridlearning.cpp" "learning/polygonlearning.cpp")
target_include_directories(AIPackaging_Learning PUBLIC "${AIPACKAGING_NESTING_INCLUDE_DIR}")
target_link_libraries(AIPackaging_Learning PUBLIC AIPackaging_GridCore AIPackaging_PolygonCore)
set_target_properties(AIPackaging_Learning PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_sources(AIPackaging_Learning PUBLIC
  FILE_SET HEADERS
  BASE_DIRS "${AIPACKAGING_NESTING_INCLUDE_DIR}"
  FILES
    "include/aipackaging/nesting/grid_learning.h"
    "include/aipackaging/nesting/polygon_learning.h"
)

add_library(AIPackaging_SolverImpl INTERFACE)
target_link_libraries(AIPackaging_SolverImpl INTERFACE
  AIPackaging_Search AIPackaging_Json AIPackaging_Learning
)

# Совместимая цель сохраняет прежний интерфейс сборки для внешних потребителей плоских заголовков.
add_library(AIPackaging_Solver INTERFACE)
target_include_directories(AIPackaging_Solver INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(AIPackaging_Solver INTERFACE AIPackaging_SolverImpl)
