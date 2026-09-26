#ifndef AIPACKAGING_CLI_COMMANDS_H
#define AIPACKAGING_CLI_COMMANDS_H

#include <iosfwd>

#include <cli_types.h>

namespace aipackaging::cli
{
/// Печатает неизменную справку по поддерживаемым командам и параметрам.
void printUsage(std::ostream & diagnostics);
/// Проверяет пару задачи и решения и возвращает согласованный код завершения.
CliExitCode runValidate(const ValidateCommand & command, std::ostream & diagnostics);
/// Проверяет внешний комплект модели либо сообщает о недоступности режима сборки.
CliExitCode runValidateModel(const ValidateModelCommand & command, std::ostream & output, std::ostream & diagnostics);
/// Загружает задачу, запускает выбранный решатель и сохраняет решение.
CliExitCode runSolve(const SolveCommand & command, std::ostream & diagnostics);
/// Разбирает аргументы, выполняет выбранную команду и возвращает числовой код процесса.
int runCli(int argc, char ** argv, std::ostream & output, std::ostream & diagnostics);
} // namespace aipackaging::cli

#endif
