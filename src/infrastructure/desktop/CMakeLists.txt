# Адаптеры локального настольного запуска полигонального раскроя.
add_library(AIPackaging_DesktopInfrastructure STATIC
  "nesting_job_runner.cpp"
  "polygon_artifact_store.cpp"
  "polygon_backends.cpp"
  "polygon_document_gateway.cpp"
  "polygon_model_jobs.cpp"
  "polygon_presentation_mapper.cpp"
)
target_include_directories(AIPackaging_DesktopInfrastructure PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(AIPackaging_DesktopInfrastructure
  PUBLIC AIPackaging_Application
  PRIVATE AIPackaging_Search AIPackaging_Json AIPackaging_PolygonCore
)
if (BUILD_ONNX_BACKEND)
  target_link_libraries(AIPackaging_DesktopInfrastructure PRIVATE AIPackaging_OnnxInference)
  target_compile_definitions(AIPackaging_DesktopInfrastructure PUBLIC AIPACKAGING_HAS_ONNX_BACKEND=1)
endif()
target_compile_features(AIPackaging_DesktopInfrastructure PUBLIC cxx_std_20)
