# Независимый прикладной контур полигональной рабочей области.
add_library(AIPackaging_Application STATIC
  "polygonworkspacecontroller.cpp"
)
target_include_directories(AIPackaging_Application PUBLIC "${CMAKE_SOURCE_DIR}/src/core/app/")
target_compile_features(AIPackaging_Application PUBLIC cxx_std_20)

# Статическая библиотека унаследованных контроллеров.
if ((BUILD_DESKTOP AND BUILD_LEGACY_DESKTOP) OR BUILD_LEGACY_TESTS)
set(app_static_lib_name "AIPackaging_App")

# Цель сборки - статическая библиотека
add_library(${app_static_lib_name} STATIC)

# Указываем какие файлы необходимо включить в цель сборки.
target_sources(${app_static_lib_name} PRIVATE
"commandmanager.cpp"
"autopackcommand.cpp"
"maincontroller.cpp"
"packingcontroller.cpp"
"packagingstate.cpp"
"movecommand.cpp"

"deletefigurecommand.cpp"
"placepreviewfigurecommand.cpp"
"rotatecommand.cpp"
"drawcontroller.cpp"
"createfigurecommand.cpp"
"services/packing/automaticpackplanner.cpp"
"services/packing/firstfitautomaticplacementstrategy.cpp"
"services/packing/figurepool.cpp"
"services/packing/packingactionmapper.cpp"
"services/packing/placementvalidator.cpp"
"services/packing/previewautomaticplacementstrategy.cpp"
"services/packing/previewpoolpresenter.cpp"
"services/io/objectpoolloader.cpp"
"services/io/objectpoolsaver.cpp"
"services/io/packingsceneloader.cpp"
"services/io/packingscenesaver.cpp"
"services/applicationstateservice.cpp"

"editorstate.cpp" )

# Указываем подключенные директории
target_include_directories(${app_static_lib_name} PUBLIC
"${CMAKE_SOURCE_DIR}/src/core/app/"
"${CMAKE_SOURCE_DIR}/src/core/app/services"
"${CMAKE_SOURCE_DIR}/src/core/app/services/packing"
"${CMAKE_SOURCE_DIR}/src/core/app/services/io"
)

# Указываем библиотеки для линковки
target_link_libraries(${app_static_lib_name} PUBLIC
AIPackaging_Domain
AIPackaging_Contract
)
endif()
