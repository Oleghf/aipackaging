# Независимый от Qt прикладной сценарий полигональной рабочей области.
add_library(AIPackaging_Application STATIC
  "activepolygondocument.cpp"
  "polygondocumentcontroller.cpp"
  "polygonworkspacecontroller.cpp"
)
target_include_directories(AIPackaging_Application PUBLIC "${CMAKE_SOURCE_DIR}/src/core/app/")
target_link_libraries(AIPackaging_Application PUBLIC AIPackaging_Editor)
target_compile_features(AIPackaging_Application PUBLIC cxx_std_20)
