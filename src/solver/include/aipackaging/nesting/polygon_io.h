#ifndef AIPACKAGING_NESTING_POLYGON_IO_H
#define AIPACKAGING_NESTING_POLYGON_IO_H

#include <string>

#include <aipackaging/nesting/polygon_types.h>

namespace aipackaging::solver
{
/// Результат строгой загрузки задачи `polygon_problem` v1.
struct PolygonProblemLoadResult
{
  bool success = false;
  PolygonProblem problem;
  std::string error;
};

/// Результат строгой загрузки решения `polygon_solution` v1.
struct PolygonSolutionLoadResult
{
  bool success = false;
  PolygonSolution solution;
  std::string error;
};

/// Загружает и нормализуемо проверяет задачу из JSON-текста.
PolygonProblemLoadResult loadPolygonProblemFromText(const std::string & text);
/// Загружает и проверяет полигональную задачу из файла.
PolygonProblemLoadResult loadPolygonProblemFromFile(const std::string & filePath);
/// Сериализует исходную полигональную задачу в канонический JSON.
std::string savePolygonProblemToText(const PolygonProblem & problem);
/// Загружает структуру полигонального решения из JSON-текста.
PolygonSolutionLoadResult loadPolygonSolutionFromText(const std::string & text);
/// Загружает структуру полигонального решения из файла.
PolygonSolutionLoadResult loadPolygonSolutionFromFile(const std::string & filePath);
/// Сериализует полигональное решение в канонический JSON.
std::string savePolygonSolutionToText(const PolygonSolution & solution);
/// Записывает полигональное решение в файл либо возвращает ошибку I/O.
bool savePolygonSolutionToFile(const std::string & filePath, const PolygonSolution & solution, std::string & error);
/// Возвращает значение корневого поля `format` без принятия предметного решения.
std::string detectJsonFormat(const std::string & text);
} // namespace aipackaging::solver

#endif
