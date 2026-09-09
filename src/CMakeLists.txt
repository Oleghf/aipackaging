set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Ищем Qt
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# Подключаем другие CMakeLists
add_subdirectory("core/domain/")
add_subdirectory("core/contract/")
add_subdirectory("gui/")
add_subdirectory("core/app/")

add_executable(${PROJECT_NAME}
"main.cpp" )

# Указываем подключенные директории
target_include_directories(${PROJECT_NAME} PUBLIC
"${CMAKE_SOURCE_DIR}/src"
)

# Указываем библиотеки для линковки
target_link_libraries(${PROJECT_NAME} PRIVATE
AIPackaging_App
AIPackaging_GUI
AIPackaging_Math
)

# Копируем dll в папку с проектом
copy_target_dll(${PROJECT_NAME} AIPackaging_Math)
copy_target_qt_dll(${PROJECT_NAME})
