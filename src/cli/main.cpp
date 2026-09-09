#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

#include <gridio.h>
#include <gridsolver.h>

namespace
{
using namespace aipackaging::solver;

/// Печатает публичный контракт команды solve и доступных параметров.
void printUsage()
{
  std::cerr << "Usage: AIPackaging_Cli solve --input <problem.json> --output <solution.json>\n"
               "       [--solver input-first-fit|area-left-bottom|max-side-left-bottom|random-left-bottom|beam]\n"
               "       [--seed N] [--random-iterations N] [--beam-width N]\n"
               "       [--max-expanded-states N] [--timeout-ms N]\n";
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

/// Забирает следующее значение argv для параметра с обязательным аргументом.
bool readValue(int argc, char ** argv, int & index, std::string & value)
{
  if (index + 1 >= argc)
    return false;
  value = argv[++index];
  return true;
}

/// Читает CLI-значение, проверяя его диапазон для size_t текущей платформы.
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
} // namespace

/// Разбирает команду, загружает задачу, запускает solver и записывает решение с договорённым exit code.
int main(int argc, char ** argv)
{
  using namespace aipackaging::solver;
  try
  {
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
          std::cerr << "Unknown solver\n";
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
        std::cerr << "Unknown option: " << option << '\n';
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
    const GridProblemLoadResult loaded = loadGridProblemFromFile(inputPath);
    if (!loaded.success)
    {
      std::cerr << loaded.error << '\n';
      return 3;
    }

    const GridSolution solution = solveGridProblem(loaded.problem, config);
    std::string outputError;
    // Для любого валидного входа сохраняется и полный, и диагностический partial.
    if (!saveGridSolutionToFile(outputPath, solution, outputError))
    {
      std::cerr << outputError << '\n';
      return 1;
    }
    if (solution.status == SolveStatus::InvalidProblem)
    {
      std::cerr << solution.errorMessage << '\n';
      return 3;
    }
    return solution.complete() ? 0 : 2;
  }
  catch (const std::exception & error)
  {
    std::cerr << "Internal error: " << error.what() << '\n';
    return 1;
  }
}
