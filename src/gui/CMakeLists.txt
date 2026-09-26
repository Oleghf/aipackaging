set(gui_static_lib_name "AIPackaging_GUI")

add_library(${gui_static_lib_name} STATIC)

target_sources(${gui_static_lib_name} PRIVATE
"polygoncanvaswidget.cpp" "polygondocumentpanel.cpp" "polygonrunpanel.cpp" "polygonstatuspanel.cpp"
"polygonworkspacewidget.cpp" "polygonstartpage.cpp" "qtapplicationdispatcher.cpp" "polygonmainwindow.cpp")

target_include_directories(${gui_static_lib_name} PUBLIC
"${CMAKE_SOURCE_DIR}/src/gui"
)

# GUI использует только нейтральные прикладные контракты; решатель и ONNX остаются в инфраструктуре.
target_link_libraries(${gui_static_lib_name} PUBLIC
Qt6::Core
Qt6::Widgets

AIPackaging_Application
)
