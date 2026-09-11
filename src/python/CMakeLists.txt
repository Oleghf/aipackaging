find_package(Python 3.12...<3.14 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 3.1 CONFIG REQUIRED)

pybind11_add_module(_aipackaging_solver MODULE
  bindings.cpp
  bindingscommon.cpp
  bindingsgrid.cpp
  bindingspolygon.cpp
)
target_link_libraries(_aipackaging_solver PRIVATE
  AIPackaging_Search AIPackaging_Json AIPackaging_Learning
)
target_compile_definitions(_aipackaging_solver PRIVATE
  AIPACKAGING_PYTHON_VERSION="${PROJECT_VERSION}"
  AIPACKAGING_BUILD_REVISION="${AIPACKAGING_BUILD_REVISION}"
)
if (MINGW)
  # Editable-сборка может выбрать MinGW из PATH. Статическое подключение его
  # runtime сохраняет wheel самодостаточным и не влияет на сборку MSVC/Linux.
  target_link_options(_aipackaging_solver PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
  get_filename_component(AIPACKAGING_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
  find_file(AIPACKAGING_WINPTHREAD_DLL libwinpthread-1.dll PATHS "${AIPACKAGING_MINGW_BIN}" NO_DEFAULT_PATH REQUIRED)
  install(FILES "${AIPACKAGING_WINPTHREAD_DLL}" DESTINATION aipackaging_ml)
endif()

install(TARGETS _aipackaging_solver
  LIBRARY DESTINATION aipackaging_ml
  RUNTIME DESTINATION aipackaging_ml
)
