set(AIPACKAGING_EDITOR_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Независимая модель редактируемого полигонального документа.
add_library(AIPackaging_Editor STATIC
  "polygon_document.cpp"
  "polygon_document_validation.cpp"
)
target_include_directories(AIPackaging_Editor PUBLIC "${AIPACKAGING_EDITOR_INCLUDE_DIR}")
target_compile_features(AIPackaging_Editor PUBLIC cxx_std_20)
target_sources(AIPackaging_Editor PUBLIC
  FILE_SET HEADERS
  BASE_DIRS "${AIPACKAGING_EDITOR_INCLUDE_DIR}"
  FILES
    "include/aipackaging/editor/polygon_document.h"
    "include/aipackaging/editor/polygon_document_validation.h"
)
