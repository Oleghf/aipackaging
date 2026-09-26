add_library(AIPackaging_CliSupport STATIC
  "cli_arguments.cpp"
  "cli_boundary.cpp"
  "cli_commands.cpp"
)
target_include_directories(AIPackaging_CliSupport PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(AIPackaging_CliSupport PRIVATE AIPackaging_Search AIPackaging_Json)

add_executable(AIPackaging_Cli "main.cpp")
target_link_libraries(AIPackaging_Cli PRIVATE AIPackaging_CliSupport)
if (BUILD_ONNX_BACKEND)
  target_link_libraries(AIPackaging_CliSupport PRIVATE AIPackaging_OnnxInference)
  target_compile_definitions(AIPackaging_CliSupport PUBLIC AIPACKAGING_HAS_ONNX_BACKEND=1)
  aipackaging_copy_onnxruntime(AIPackaging_Cli)
endif()
