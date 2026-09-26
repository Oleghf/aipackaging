include(CMakeParseArguments)

# Проверяет прямые и публично транзитивные зависимости одной цели сборки.
function(aipackaging_assert_target_dependencies)
  set(options ALLOW_QT)
  set(one_value_args TARGET)
  set(multi_value_args ALLOWED_DIRECT ALLOWED_PUBLIC)
  cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if (NOT ARG_TARGET OR NOT TARGET "${ARG_TARGET}")
    message(FATAL_ERROR "Architecture check: unknown target '${ARG_TARGET}'")
  endif()

  get_target_property(direct_dependencies "${ARG_TARGET}" LINK_LIBRARIES)
  if (NOT direct_dependencies)
    set(direct_dependencies "")
  endif()

  foreach(dependency IN LISTS direct_dependencies)
    string(REGEX REPLACE "^\\$<LINK_ONLY:([^>]+)>$" "\\1" dependency "${dependency}")
    if (dependency MATCHES "^AIPackaging_" AND NOT dependency IN_LIST ARG_ALLOWED_DIRECT)
      message(FATAL_ERROR
        "Architecture check: forbidden direct target edge ${ARG_TARGET} -> ${dependency}")
    endif()
    if (dependency MATCHES "^Qt[0-9]+::" AND NOT ARG_ALLOW_QT)
      message(FATAL_ERROR
        "Architecture check: Qt dependency is forbidden for ${ARG_TARGET}: ${dependency}")
    endif()
  endforeach()

  set(queue "${ARG_TARGET}")
  set(visited "")
  while(queue)
    list(POP_FRONT queue current_target)
    if (current_target IN_LIST visited OR NOT TARGET "${current_target}")
      continue()
    endif()
    list(APPEND visited "${current_target}")
    get_target_property(interface_dependencies "${current_target}" INTERFACE_LINK_LIBRARIES)
    if (NOT interface_dependencies)
      continue()
    endif()
    foreach(dependency IN LISTS interface_dependencies)
      string(REGEX REPLACE "^\\$<LINK_ONLY:([^>]+)>$" "\\1" dependency "${dependency}")
      if (dependency MATCHES "^AIPackaging_" AND
          NOT dependency STREQUAL ARG_TARGET AND
          NOT dependency IN_LIST ARG_ALLOWED_PUBLIC)
        message(FATAL_ERROR
          "Architecture check: forbidden public target edge ${ARG_TARGET} -> ${dependency}")
      endif()
      if (dependency MATCHES "^Qt[0-9]+::" AND NOT ARG_ALLOW_QT)
        message(FATAL_ERROR
          "Architecture check: transitive Qt dependency is forbidden for ${ARG_TARGET}: ${dependency}")
      endif()
      if (TARGET "${dependency}")
        list(APPEND queue "${dependency}")
      endif()
    endforeach()
  endwhile()
endfunction()

# Фиксирует разрешённый граф рабочих целей сборки после их создания.
function(aipackaging_check_production_target_graph)
  foreach(retired_target IN ITEMS AIPackaging_Math AIPackaging_Domain AIPackaging_Contract AIPackaging_App)
    if (TARGET "${retired_target}")
      message(FATAL_ERROR "Architecture check: retired desktop target ${retired_target} must not be created")
    endif()
  endforeach()
  aipackaging_assert_target_dependencies(TARGET AIPackaging_NestingCore)
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_SearchContracts
    ALLOWED_DIRECT AIPackaging_NestingCore
    ALLOWED_PUBLIC AIPackaging_NestingCore
  )
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_GridCore
    ALLOWED_DIRECT AIPackaging_NestingCore
    ALLOWED_PUBLIC AIPackaging_NestingCore
  )
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_PolygonCore
    ALLOWED_DIRECT AIPackaging_NestingCore
    ALLOWED_PUBLIC AIPackaging_NestingCore
  )
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_Search
    ALLOWED_DIRECT AIPackaging_SearchContracts AIPackaging_GridCore AIPackaging_PolygonCore
    ALLOWED_PUBLIC AIPackaging_SearchContracts AIPackaging_GridCore AIPackaging_PolygonCore AIPackaging_NestingCore
  )
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_Json
    ALLOWED_DIRECT AIPackaging_SearchContracts AIPackaging_GridCore AIPackaging_PolygonCore
    ALLOWED_PUBLIC AIPackaging_SearchContracts AIPackaging_GridCore AIPackaging_PolygonCore AIPackaging_NestingCore
  )
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_Learning
    ALLOWED_DIRECT AIPackaging_GridCore AIPackaging_PolygonCore
    ALLOWED_PUBLIC AIPackaging_GridCore AIPackaging_PolygonCore AIPackaging_NestingCore
  )
  set(aipackaging_nesting_modules
    AIPackaging_SearchContracts
    AIPackaging_GridCore
    AIPackaging_PolygonCore
    AIPackaging_Search
    AIPackaging_Json
    AIPackaging_Learning
    AIPackaging_NestingCore
  )
  if (TARGET AIPackaging_OnnxInference)
    aipackaging_assert_target_dependencies(
      TARGET AIPackaging_OnnxInference
      ALLOWED_DIRECT AIPackaging_Learning AIPackaging_Search
      ALLOWED_PUBLIC AIPackaging_Learning AIPackaging_Search ${aipackaging_nesting_modules}
    )
  endif()
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_SolverImpl
    ALLOWED_PUBLIC ${aipackaging_nesting_modules}
  )
  aipackaging_assert_target_dependencies(
    TARGET AIPackaging_Solver
    ALLOWED_PUBLIC AIPackaging_SolverImpl ${aipackaging_nesting_modules}
  )

  if (TARGET AIPackaging_Application)
    aipackaging_assert_target_dependencies(
      TARGET AIPackaging_Application
    )
    aipackaging_assert_target_dependencies(
      TARGET AIPackaging_DesktopInfrastructure
      ALLOWED_DIRECT AIPackaging_Application AIPackaging_Search AIPackaging_Json AIPackaging_PolygonCore AIPackaging_OnnxInference
      ALLOWED_PUBLIC AIPackaging_Application AIPackaging_OnnxInference ${aipackaging_nesting_modules}
    )
  endif()

  if (TARGET AIPackaging_CliSupport)
    aipackaging_assert_target_dependencies(
      TARGET AIPackaging_CliSupport
      ALLOWED_DIRECT AIPackaging_Search AIPackaging_Json AIPackaging_OnnxInference
      ALLOWED_PUBLIC AIPackaging_OnnxInference ${aipackaging_nesting_modules}
    )
  endif()
  if (TARGET AIPackaging_Cli)
    aipackaging_assert_target_dependencies(
      TARGET AIPackaging_Cli
      ALLOWED_DIRECT AIPackaging_CliSupport
      ALLOWED_PUBLIC AIPackaging_CliSupport AIPackaging_OnnxInference ${aipackaging_nesting_modules}
    )
  endif()
  if (TARGET _aipackaging_solver)
    aipackaging_assert_target_dependencies(
      TARGET _aipackaging_solver
      ALLOWED_DIRECT AIPackaging_Search AIPackaging_Json AIPackaging_Learning
      ALLOWED_PUBLIC ${aipackaging_nesting_modules}
    )
  endif()
  if (TARGET AIPackaging_GUI)
    aipackaging_assert_target_dependencies(
      TARGET AIPackaging_GUI
      ALLOW_QT
      ALLOWED_DIRECT AIPackaging_Application
      ALLOWED_PUBLIC AIPackaging_Application
    )
  endif()
  if (TARGET AIPackaging)
    aipackaging_assert_target_dependencies(
      TARGET AIPackaging
      ALLOW_QT
      ALLOWED_DIRECT AIPackaging_Application AIPackaging_DesktopInfrastructure AIPackaging_GUI
    )
  endif()
endfunction()
