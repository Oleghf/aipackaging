# Независимый от Qt прикладной сценарий полигональной рабочей области.
add_library(AIPackaging_Application STATIC
  "activepolygondocument.cpp"
  "polygonworkspacecontroller.cpp"
)
target_include_directories(AIPackaging_Application PUBLIC "${CMAKE_SOURCE_DIR}/src/core/app/")
target_compile_features(AIPackaging_Application PUBLIC cxx_std_20)
