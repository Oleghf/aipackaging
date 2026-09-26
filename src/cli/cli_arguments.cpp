#include <charconv>
#include <limits>
#include <string>

#include <aipackaging/nesting/search_contracts.h>
#include <cli_arguments.h>

namespace aipackaging::cli
{
namespace
{
/// Возвращает отказ разбора с требуемыми диагностикой и признаком показа справки.
ParseResult failure(std::string diagnostic = {}, bool showUsage = false)
{
  return {std::nullopt, std::move(diagnostic), showUsage};
}

/// Строго разбирает неотрицательное десятичное число без пробелов и суффиксов.
bool parseUnsigned(std::string_view text, std::uint64_t & value)
{
  if (text.empty())
    return false;
  const char * begin = text.data();
  const char * end = begin + text.size();
  const auto [position, error] = std::from_chars(begin, end, value);
  return error == std::errc{} && position == end;
}

/// Забирает следующее значение массива аргументов для параметра с обязательным значением.
bool readValue(std::span<const std::string_view> arguments, std::size_t & index, std::string & value)
{
  if (index + 1 >= arguments.size())
    return false;
  value = arguments[++index];
  return true;
}

/// Читает значение параметра с проверкой диапазона типа `size_t` текущей платформы.
bool readSizeOption(std::span<const std::string_view> arguments, std::size_t & index, std::size_t & value)
{
  std::string text;
  std::uint64_t parsed = 0;
  if (!readValue(arguments, index, text) || !parseUnsigned(text, parsed) || parsed > std::numeric_limits<std::size_t>::max())
  {
    return false;
  }
  value = static_cast<std::size_t>(parsed);
  return true;
}

/// Разбирает параметры проверки согласованной пары задачи и решения.
ParseResult parseValidate(std::span<const std::string_view> arguments)
{
  ValidateCommand command;
  for (std::size_t index = 2; index < arguments.size(); ++index)
  {
    const std::string_view option = arguments[index];
    if (option == "--problem")
    {
      if (!readValue(arguments, index, command.problemPath))
        return failure();
    }
    else if (option == "--solution")
    {
      if (!readValue(arguments, index, command.solutionPath))
        return failure();
    }
    else
      return failure();
  }
  return {ParsedCommand{std::move(command)}, {}, false};
}

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
/// Разбирает путь к комплекту модели для команды `validate-model`.
ParseResult parseValidateModel(std::span<const std::string_view> arguments)
{
  ValidateModelCommand command;
  for (std::size_t index = 2; index < arguments.size(); ++index)
  {
    if (arguments[index] != "--input" || !readValue(arguments, index, command.inputPath))
      return failure();
  }
  return {ParsedCommand{std::move(command)}, {}, false};
}
#endif

/// Разбирает параметры запуска решателя, сохраняя прежние значения по умолчанию.
ParseResult parseSolve(std::span<const std::string_view> arguments)
{
  SolveCommand command;
  for (std::size_t index = 2; index < arguments.size(); ++index)
  {
    const std::string_view option = arguments[index];
    std::string value;
    if (option == "--input")
    {
      if (!readValue(arguments, index, command.inputPath))
        return failure({}, true);
    }
    else if (option == "--output")
    {
      if (!readValue(arguments, index, command.outputPath))
        return failure({}, true);
    }
    else if (option == "--solver")
    {
      if (!readValue(arguments, index, value))
        return failure({}, true);
      command.solverName = value;
      solver::SolverKind solver;
      if (value != "neural-greedy" && value != "neural-best-of" && value != "hybrid" && !solver::parseSolverKind(value, solver))
      {
        return failure("Неизвестный решатель\n");
      }
    }
    else if (option == "--seed")
    {
      if (!readValue(arguments, index, value) || !parseUnsigned(value, command.seed))
        return failure({}, true);
    }
    else if (option == "--random-iterations")
    {
      if (!readSizeOption(arguments, index, command.randomIterations) || command.randomIterations == 0)
        return failure({}, true);
    }
    else if (option == "--beam-width")
    {
      if (!readSizeOption(arguments, index, command.beamWidth) || command.beamWidth == 0)
        return failure({}, true);
    }
    else if (option == "--max-expanded-states")
    {
      if (!readSizeOption(arguments, index, command.maxExpandedStates) || command.maxExpandedStates == 0)
        return failure({}, true);
    }
    else if (option == "--timeout-ms")
    {
      if (!readValue(arguments, index, value) || !parseUnsigned(value, command.timeoutMs))
        return failure({}, true);
    }
    else if (option == "--model")
    {
      if (!readValue(arguments, index, command.modelPath))
        return failure({}, true);
    }
    else if (option == "--rollouts")
    {
      if (!readSizeOption(arguments, index, command.neuralRollouts) || command.neuralRollouts == 0)
        return failure({}, true);
    }
    else
      return failure("Неизвестный параметр: " + std::string(option) + '\n', true);
  }
  if (command.inputPath.empty() || command.outputPath.empty())
    return failure({}, true);
  return {ParsedCommand{std::move(command)}, {}, false};
}
} // namespace

/// Выбирает подкоманду и делегирует чистый разбор её параметров.
ParseResult parseArguments(std::span<const std::string_view> arguments)
{
  if (arguments.size() >= 2 && arguments[1] == "validate")
    return parseValidate(arguments);
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
  if (arguments.size() >= 2 && arguments[1] == "validate-model")
    return parseValidateModel(arguments);
#endif
  if (arguments.size() < 2 || arguments[1] != "solve")
    return failure({}, true);
  return parseSolve(arguments);
}
} // namespace aipackaging::cli
