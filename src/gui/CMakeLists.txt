# Статическая библиотека
set(gui_static_lib_name "AIPackaging_GUI")

# Цель сборки - статическая библиотека
add_library(${gui_static_lib_name} STATIC)

# Указываем какие файлы необходимо включить в цель сборки.
target_sources(${gui_static_lib_name} PRIVATE
"polygoncanvaswidget.cpp" "polygonworkspacewidget.cpp" "polygonstartpage.cpp" "qtapplicationdispatcher.cpp" "polygonmainwindow.cpp")

# Указываем подключенные директории
target_include_directories(${gui_static_lib_name} PUBLIC
"${CMAKE_SOURCE_DIR}/src/gui"
)

# Указываем библиотеки для линковки
target_link_libraries(${gui_static_lib_name} PUBLIC
Qt6::Core
Qt6::Widgets

AIPackaging_Application
)
