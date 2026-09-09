# Статическая библиотека
set(domain_static_lib_name "AIPackaging_Domain")

# Цель сборки - статическая библиотека
add_library(${domain_static_lib_name} STATIC)

# Указываем какие файлы необходимо включить в цель сборки.
target_sources(${domain_static_lib_name} PRIVATE
"selectionmodel.cpp"
"board.cpp" "boardanalyzer.cpp"
"algorithms/algs.cpp"
"algorithms/cell2d.cpp"
"algorithms/coordinates2d.cpp"
"algorithms/countour2d.cpp")

# Указываем подключенные директории
target_include_directories(${domain_static_lib_name} PUBLIC
"${CMAKE_SOURCE_DIR}/src/core/domain"
"algorithms/"
)

# Указываем библиотеки для линковки
target_link_libraries(${domain_static_lib_name} PUBLIC
AIPackaging_Math
AIPackaging_Contract
)
