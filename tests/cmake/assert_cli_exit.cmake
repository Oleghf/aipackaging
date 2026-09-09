if (NOT DEFINED CLI OR NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR
    NOT DEFINED SOLVER OR NOT DEFINED EXPECTED_EXIT)
  message(FATAL_ERROR "The CLI exit-code assertion is missing an input variable")
endif()

# Запускаем настоящий CLI-процесс, чтобы проверить внешний контракт, а не
# внутренний результат функции solver.
execute_process(
  COMMAND "${CLI}" solve
    --input "${INPUT}"
    --output "${OUTPUT}"
    --solver "${SOLVER}"
    --timeout-ms 0
  RESULT_VARIABLE actual_exit
  OUTPUT_VARIABLE cli_stdout
  ERROR_VARIABLE cli_stderr
)

# Тест различает конкретные категории ошибок: любой другой ненулевой код не
# должен маскировать поломку загрузки, поиска или записи результата.
if (NOT actual_exit EQUAL EXPECTED_EXIT)
  message(FATAL_ERROR
    "Expected CLI exit ${EXPECTED_EXIT}, got ${actual_exit}\n"
    "stdout:\n${cli_stdout}\n"
    "stderr:\n${cli_stderr}"
  )
endif()
