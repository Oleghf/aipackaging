#include <charconv>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

#include <aipackaging/nesting/grid_environment.h>
#include <aipackaging/nesting/grid_io.h>
#include <aipackaging/nesting/grid_solver.h>
#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_io.h>
#include <aipackaging/nesting/polygon_solver.h>

namespace
{
using namespace aipackaging::solver;

/// Печатает справку по команде `solve` и доступным параметрам.
void printUsage()
{
  std::cerr << "Использование: `AIPackaging_Cli solve --input <problem.json> --output <solution.json>`\n"
               "       `[--solver input-first-fit|area-left-bottom|max-side-left-bottom|random-left-bottom|beam]`\n"
               "       `[--seed N] [--random-iterations N] [--beam-width N]`\n"
               "       `[--max-expanded-states N] [--timeout-ms N]`\n"
               "       `AIPackaging_Cli validate --problem <problem.json> --solution <solution.json>`\n";
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
bool readValue(int argc, char ** argv, int & index, std::string & value)
{
  if (index + 1 >= argc)
    return false;
  value = argv[++index];
  return true;
}

/// Читает значение CLI, проверяя его диапазон для типа `size_t` текущей платформы.
bool readSizeOption(int argc, char ** argv, int & index, std::size_t & value)
{
  std::string text;
  std::uint64_t parsed = 0;
  if (!readValue(argc, argv, index, text) || !parseUnsigned(text, parsed) || parsed > std::numeric_limits<std::size_t>::max())
  {
    return false;
  }
  value = static_cast<std::size_t>(parsed);
  return true;
}

/// Читает файл целиком для определения формата обмена до синтаксического разбора.
bool readTextFile(const std::string & path, std::string & text)
{
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  return static_cast<bool>(input) || input.eof();
}

/// Проверяет согласованную пару клеточной или полигональной задачи и решения.
int validateCommand(int argc, char ** argv)
{
  std::string problemPath;
  std::string solutionPath;
  for (int index = 2; index < argc; ++index)
  {
    const std::string_view option = argv[index];
    if (option == "--problem")
    {
      if (!readValue(argc, argv, index, problemPath))
        return 1;
    }
    else if (option == "--solution")
    {
      if (!readValue(argc, argv, index, solutionPath))
        return 1;
    }
    else
      return 1;
  }
  std::string problemText;
  std::string solutionText;
  if (problemPath.empty() || solutionPath.empty() || !readTextFile(problemPath, problemText) ||
      !readTextFile(solutionPath, solutionText))
  {
    std::cerr << "Не удалось прочитать входные данные проверки\n";
    return 1;
  }
  const std::string format = detectJsonFormat(problemText);
  if (format == "aipackaging.grid_problem")
  {
    const GridProblemLoadResult problem = loadGridProblemFromText(problemText);
    const GridSolutionLoadResult solution = loadGridSolutionFromText(solutionText);
    const ValidationResult result = problem.success && solution.success
                                    ? validateGridSolution(problem.problem, solution.solution)
                                    : ValidationResult{false, problem.success ? solution.error : problem.error};
    if (!result.success)
      std::cerr << result.error << '\n';
    return result.success ? 0 : 3;
  }
  if (format == "aipackaging.polygon_problem")
  {
    const PolygonProblemLoadResult problem = loadPolygonProblemFromText(problemText);
    const PolygonSolutionLoadResult solution = loadPolygonSolutionFromText(solutionText);
    const ValidationResult result = problem.success && solution.success
                                    ? validatePolygonSolution(problem.problem, solution.solution)
                                    : ValidationResult{false, problem.success ? solution.error : problem.error};
    if (!result.success)
      std::cerr << result.error << '\n';
    return result.success ? 0 : 3;
  }
  std::cerr << "Неподдерживаемый формат задачи\n";
  return 3;
}
} // namespace

/// Разбирает команду, загружает задачу, запускает решатель и возвращает согласованный код.
int main(int argc, char ** argv)
{
  using namespace aipackaging::solver;
  try
  {
    if (argc >= 2 && std::string_view(argv[1]) == "validate")
      return validateCommand(argc, argv);
    if (argc < 2 || std::string_view(argv[1]) != "solve")
    {
      printUsage();
      return 1;
    }

    std::string inputPath;
    std::string outputPath;
    SolverConfig config;
    for (int index = 2; index < argc; ++index)
    {
      const std::string_view option = argv[index];
      std::string value;
      if (option == "--input")
      {
        if (!readValue(argc, argv, index, inputPath))
          return printUsage(), 1;
      }
      else if (option == "--output")
      {
        if (!readValue(argc, argv, index, outputPath))
          return printUsage(), 1;
      }
      else if (option == "--solver")
      {
        if (!readValue(argc, argv, index, value) || !parseSolverKind(value, config.solver))
        {
          std::cerr << "Неизвестный решатель\n";
          return 1;
        }
      }
      else if (option == "--seed")
      {
        if (!readValue(argc, argv, index, value) || !parseUnsigned(value, config.seed))
          return printUsage(), 1;
      }
      else if (option == "--random-iterations")
      {
        if (!readSizeOption(argc, argv, index, config.randomIterations) || config.randomIterations == 0)
          return printUsage(), 1;
      }
      else if (option == "--beam-width")
      {
        if (!readSizeOption(argc, argv, index, config.beamWidth) || config.beamWidth == 0)
          return printUsage(), 1;
      }
      else if (option == "--max-expanded-states")
      {
        if (!readSizeOption(argc, argv, index, config.maxExpandedStates) || config.maxExpandedStates == 0)
          return printUsage(), 1;
      }
      else if (option == "--timeout-ms")
      {
        if (!readValue(argc, argv, index, value) || !parseUnsigned(value, config.timeoutMs))
          return printUsage(), 1;
      }
      else
      {
        std::cerr << "Неизвестный параметр: " << option << '\n';
        printUsage();
        return 1;
      }
    }

    if (inputPath.empty() || outputPath.empty())
    {
      printUsage();
      return 1;
    }

    // Невалидный вход не порождает псевдорешение: ошибка относится к контракту
    // задачи и возвращается отдельным кодом до запуска поиска.
    std::string inputText;
    if (!readTextFile(inputPath, inputText))
    {
      std::cerr << "Не удалось открыть входной файл задачи\n";
      return 1;
    }
    const std::string format = detectJsonFormat(inputText);
    if (format == "aipackaging.polygon_problem")
    {
      const PolygonProblemLoadResult loaded = loadPolygonProblemFromText(inputText);
      if (!loaded.success)
      {
        std::cerr << loaded.error << '\n';
        return 3;
      }
      const PolygonSolution solution = solvePolygonProblem(loaded.problem, config);
      std::string outputError;
      if (!savePolygonSolutionToFile(outputPath, solution, outputError))
      {
        std::cerr << outputError << '\n';
        return 1;
      }
      return solution.complete() ? 0 : 2;
    }
    if (format != "aipackaging.grid_problem")
    {
      std::cerr << "Формат задачи отсутствует или не поддерживается\n";
      return 3;
    }
    const GridProblemLoadResult loaded = loadGridProblemFromText(inputText);
    if (!loaded.success)
    {
      std::cerr << loaded.error << '\n';
      return 3;
    }
    const GridSolution solution = solveGridProblem(loaded.problem, config);
    std::string outputError;
    if (!saveGridSolutionToFile(outputPath, solution, outputError))
    {
      std::cerr << outputError << '\n';
      return 1;
    }
    return solution.complete() ? 0 : 2;
  }
  catch (const std::exception & error)
  {
    std::cerr << "Внутренняя ошибка: " << error.what() << '\n';
    return 1;
  }
}
