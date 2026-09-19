add_library(AIPackaging_OnnxInference STATIC
  "polygononnxinference.cpp"
)
target_include_directories(AIPackaging_OnnxInference PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(AIPackaging_OnnxInference
  PUBLIC AIPackaging_Learning AIPackaging_Search
  PRIVATE nlohmann_json::nlohmann_json onnxruntime::onnxruntime
)
target_compile_features(AIPackaging_OnnxInference PUBLIC cxx_std_20)
target_compile_definitions(AIPackaging_OnnxInference PRIVATE
  AIPACKAGING_PROJECT_VERSION="${PROJECT_VERSION}"
  AIPACKAGING_BUILD_REVISION="${AIPACKAGING_BUILD_REVISION}"
)
target_sources(AIPackaging_OnnxInference PUBLIC
  FILE_SET HEADERS
  BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
  FILES "include/aipackaging/inference/polygon_onnx.h"
)

function(aipackaging_copy_onnxruntime target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:onnxruntime::onnxruntime>"
      "$<TARGET_FILE_DIR:${target}>"
    VERBATIM
  )
endfunction()
