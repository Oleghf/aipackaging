add_library(AIPackaging_NestingCore STATIC "common_contracts.cpp")

target_sources(AIPackaging_NestingCore PUBLIC
  FILE_SET HEADERS
  BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
  FILES "include/aipackaging/nesting/common_contracts.h"
)
target_include_directories(AIPackaging_NestingCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(AIPackaging_NestingCore PUBLIC cxx_std_20)
set_target_properties(AIPackaging_NestingCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
