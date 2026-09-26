#ifndef AIPACKAGING_CLI_TYPES_H
#define AIPACKAGING_CLI_TYPES_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace aipackaging::cli
{
/// Согласованные коды завершения командной строки.
enum class CliExitCode : int
{
  Success = 0,
  Failure = 1,
  PartialSolution = 2,
  InvalidData = 3
};

/// Проверенные синтаксические параметры команды `solve` без операций ввода-вывода.
struct SolveCommand
{
  std::string inputPath;
  std::string outputPath;
  std::string solverName = "area-left-bottom";
  std::string modelPath;
  std::uint64_t seed = 42;
  std::size_t randomIterations = 64;
  std::size_t beamWidth = 32;
  std::size_t maxExpandedStates = 50000;
  std::uint64_t timeoutMs = 30000;
  std::size_t neuralRollouts = 16;
};

/// Проверенные синтаксические параметры команды `validate`.
struct ValidateCommand
{
  std::string problemPath;
  std::string solutionPath;
};

/// Проверенные синтаксические параметры команды `validate-model`.
struct ValidateModelCommand
{
  std::string inputPath;
};

/// Одна из поддерживаемых команд после успешного синтаксического разбора.
using ParsedCommand = std::variant<SolveCommand, ValidateCommand, ValidateModelCommand>;

/// Результат чистого разбора аргументов с инструкцией по выводу прежней диагностики.
struct ParseResult
{
  std::optional<ParsedCommand> command;
  std::string diagnostic;
  bool showUsage = false;
};
} // namespace aipackaging::cli

#endif
