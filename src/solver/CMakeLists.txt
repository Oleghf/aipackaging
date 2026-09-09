add_library(AIPackaging_Solver STATIC)

target_sources(AIPackaging_Solver PRIVATE
  "gridenvironment.cpp"
  "gridio.cpp"
  "gridsolver.cpp"
)

target_include_directories(AIPackaging_Solver PUBLIC "${CMAKE_SOURCE_DIR}/src/solver")
target_link_libraries(AIPackaging_Solver PRIVATE nlohmann_json::nlohmann_json)

target_compile_definitions(AIPackaging_Solver PRIVATE
  AIPACKAGING_PROJECT_VERSION="${PROJECT_VERSION}"
  AIPACKAGING_BUILD_REVISION="${AIPACKAGING_BUILD_REVISION}"
)
