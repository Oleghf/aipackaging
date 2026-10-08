set(gui_static_lib_name "AIPackaging_GUI")

add_library(${gui_static_lib_name} STATIC)

target_sources(${gui_static_lib_name} PRIVATE
"polygoncanvaswidget.cpp" "polygoncanvasrenderer.cpp" "polygoneditorcanvasinteraction.cpp" "polygoneditortoolbar.cpp" "polygonproblemspanel.cpp"
"polygondocumentpanel.cpp" "polygoneditorpanel.cpp" "polygonrunpanel.cpp" "polygonstatuspanel.cpp"
"polygonworkspacewidget.cpp" "polygonstartpage.cpp" "qtapplicationdispatcher.cpp" "polygonmainwindow.cpp")
target_sources(${gui_static_lib_name} PRIVATE "polygondxfimportwizard.cpp")
target_sources(${gui_static_lib_name} PRIVATE "qtlocalization.cpp")
target_sources(${gui_static_lib_name} PRIVATE "polygonmainwindowhelp.cpp")
target_compile_definitions(${gui_static_lib_name} PRIVATE
  AIPACKAGING_PROJECT_VERSION="${PROJECT_VERSION}" AIPACKAGING_BUILD_REVISION="${AIPACKAGING_BUILD_REVISION}")
if(BUILD_ONNX_BACKEND)
  target_compile_definitions(${gui_static_lib_name} PRIVATE AIPACKAGING_HAS_ONNX_BACKEND=1)
endif()

# Перевод входит в ресурс библиотеки и не требует установленного Qt на машине пользователя.
get_filename_component(qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
find_file(qt_russian_translation qtbase_ru.qm
  PATHS "${qt_prefix}/translations" NO_DEFAULT_PATH REQUIRED)
get_filename_component(qt_translation_directory "${qt_russian_translation}" DIRECTORY)
configure_file("polygon_translations.qrc.in" "${CMAKE_CURRENT_BINARY_DIR}/polygon_translations.qrc" @ONLY)
qt_add_resources(qt_russian_sources "${CMAKE_CURRENT_BINARY_DIR}/polygon_translations.qrc")
target_sources(${gui_static_lib_name} PRIVATE ${qt_russian_sources})

target_include_directories(${gui_static_lib_name} PUBLIC
"${CMAKE_SOURCE_DIR}/src/gui"
)

# GUI использует только нейтральные прикладные контракты; решатель и ONNX остаются в инфраструктуре.
target_link_libraries(${gui_static_lib_name} PUBLIC
Qt6::Core
Qt6::Widgets

AIPackaging_Application
)
