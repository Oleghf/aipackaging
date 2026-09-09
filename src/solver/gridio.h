#ifndef AIPACKAGING_SOLVER_GRIDIO_H
#define AIPACKAGING_SOLVER_GRIDIO_H

#include <string>

#include <gridtypes.h>

namespace aipackaging::solver
{
/// Результат строгой загрузки grid_problem v1.
struct GridProblemLoadResult
{
  bool success = false;
  GridProblem problem;
  std::string error;
};

/// Результат строгой загрузки grid_solution v1.
struct GridSolutionLoadResult
{
  bool success = false;
  GridSolution solution;
  std::string error;
};

/// Загружает и проверяет задачу из JSON-текста.
GridProblemLoadResult loadGridProblemFromText(const std::string & text);
/// Загружает и проверяет задачу из JSON-файла.
GridProblemLoadResult loadGridProblemFromFile(const std::string & filePath);
/// Сериализует задачу в канонический JSON grid_problem v1.
std::string saveGridProblemToText(const GridProblem & problem);

/// Загружает структуру решения из JSON-текста без привязки к конкретной задаче.
GridSolutionLoadResult loadGridSolutionFromText(const std::string & text);
/// Загружает структуру решения из JSON-файла без привязки к конкретной задаче.
GridSolutionLoadResult loadGridSolutionFromFile(const std::string & filePath);
/// Сериализует результат в канонический JSON grid_solution v1.
std::string saveGridSolutionToText(const GridSolution & solution);
/// Записывает grid_solution v1 в файл либо возвращает диагностическое сообщение.
bool saveGridSolutionToFile(const std::string & filePath, const GridSolution & solution, std::string & error);
} // namespace aipackaging::solver

#endif
