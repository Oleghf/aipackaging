set(AIPACKAGING_EDITOR_PERSISTENCE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Строгий формат черновика остаётся адаптером над независимой моделью редактора.
add_library(AIPackaging_EditorPersistence STATIC "polygon_draft_io.cpp")
target_include_directories(AIPackaging_EditorPersistence PUBLIC "${AIPACKAGING_EDITOR_PERSISTENCE_INCLUDE_DIR}")
target_link_libraries(AIPackaging_EditorPersistence
  PUBLIC AIPackaging_Editor
  PRIVATE AIPackaging_StrictJsonSupport nlohmann_json::nlohmann_json
)
target_compile_features(AIPackaging_EditorPersistence PUBLIC cxx_std_20)
target_sources(AIPackaging_EditorPersistence PUBLIC
  FILE_SET HEADERS
  BASE_DIRS "${AIPACKAGING_EDITOR_PERSISTENCE_INCLUDE_DIR}"
  FILES "include/aipackaging/editor/polygon_draft_io.h"
)
