set(AIPACKAGING_DXF_IMPORT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Строгий импорт поддерживаемого подмножества ASCII DXF в нейтральную модель редактора.
add_library(AIPackaging_DxfImport STATIC
  "dxf_import.cpp"
)
target_include_directories(AIPackaging_DxfImport PUBLIC "${AIPACKAGING_DXF_IMPORT_INCLUDE_DIR}")
target_link_libraries(AIPackaging_DxfImport PUBLIC AIPackaging_Editor)
target_compile_features(AIPackaging_DxfImport PUBLIC cxx_std_20)
target_sources(AIPackaging_DxfImport PUBLIC
  FILE_SET HEADERS
  BASE_DIRS "${AIPACKAGING_DXF_IMPORT_INCLUDE_DIR}"
  FILES
    "include/aipackaging/import/dxf_import.h"
)
