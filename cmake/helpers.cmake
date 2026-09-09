# Копирует в папку к target DLL библиотеку
function(copy_target_dll target DLL_target)
    add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:${DLL_target}> $<TARGET_FILE_DIR:${target}>
    )
endfunction()


# Копирует нужные qt зависимости в папку к target
function(copy_target_qt_dll target)
    if(WIN32 AND TARGET Qt6::windeployqt)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND $<TARGET_FILE:Qt6::windeployqt> --$<IF:$<CONFIG:Debug>,debug,release> "$<TARGET_FILE:${target}>" VERBATIM)
    elseif(APPLE AND TARGET Qt6::macdeployqt)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND $<TARGET_FILE:Qt6::macdeployqt> "$<TARGET_FILE:${target}>" VERBATIM)
    else()
        find_program(WINDEPLOYQT windeployqt)
        find_program(LINUXDEPLOYQT linuxdeployqt)
        find_program(MACDEPLOYQT macdeployqt)
    endif()

    if(NOT (WIN32 AND TARGET Qt6::windeployqt) AND WINDEPLOYQT)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${WINDEPLOYQT} --$<IF:$<CONFIG:Debug>,debug,release> "$<TARGET_FILE:${target}>" VERBATIM)
    elseif(NOT (WIN32 AND TARGET Qt6::windeployqt) AND LINUXDEPLOYQT)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${LINUXDEPLOYQT} "$<TARGET_FILE:${target}>" VERBATIM)
    elseif(NOT (APPLE AND TARGET Qt6::macdeployqt) AND MACDEPLOYQT)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${MACDEPLOYQT} "$<TARGET_FILE:${target}>" VERBATIM)
    elseif(NOT (WIN32 AND TARGET Qt6::windeployqt) AND NOT (APPLE AND TARGET Qt6::macdeployqt))
        message(WARNING "Не удалось перенести зависимости Qt.")
    endif()
endfunction()
