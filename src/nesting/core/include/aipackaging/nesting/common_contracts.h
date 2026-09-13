#ifndef AIPACKAGING_NESTING_COMMON_CONTRACTS_H
#define AIPACKAGING_NESTING_COMMON_CONTRACTS_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace aipackaging::solver
{
/// Нейтральное описание целевой функции задачи раскроя.
struct ObjectiveDefinition
{
  std::string type = "valuable_right_remnant";
  int version = 1;
};

/// Итоговый статус попытки решить задачу раскроя.
enum class SolveStatus : std::uint8_t
{
  Solved,
  NoSolutionFound,
  BudgetExhausted,
  TimedOut,
  UnsupportedEnvironment,
  InvalidProblem
};

/// Определяет происхождение решения без привязки к конкретному алгоритму.
enum class SolverFamily : std::uint8_t
{
  Baseline,
  Neural,
  Hybrid
};

/// Счётчики работы решателя и раздельные временные измерения.
struct SolverMetrics
{
  std::uint64_t candidatesGenerated = 0;
  std::uint64_t candidatesValidated = 0;
  std::uint64_t expandedStates = 0;
  std::uint64_t candidateGenerationTimeUs = 0;
  std::uint64_t validationTimeUs = 0;
  std::uint64_t searchTimeUs = 0;
  std::uint64_t totalTimeUs = 0;
};

/// Настройки и версия кода, которыми было получено решение.
struct SolverMetadata
{
  SolverFamily family = SolverFamily::Baseline;
  std::string name;
  std::string projectVersion;
  std::string revision;
  std::uint64_t seed = 42;
  std::size_t randomIterations = 64;
  std::size_t beamWidth = 32;
  std::size_t maxExpandedStates = 50000;
  std::uint64_t timeoutMs = 30000;
  std::string modelId;
  std::string modelSha256;
  std::size_t rollouts = 0;
  std::string selectionMode;
};

/// Результат проверки публичного контракта с диагностикой ошибки.
struct ValidationResult
{
  bool success = false;
  std::string error;
};

/// Возвращает стабильное строковое имя статуса для форматов обмена и CLI.
std::string toString(SolveStatus status);
/// Возвращает стабильное строковое имя семейства решателя.
std::string toString(SolverFamily family);
/// Преобразует публичное строковое имя в статус решения.
bool parseSolveStatus(const std::string & value, SolveStatus & status);
/// Преобразует публичное строковое имя в семейство решателя.
bool parseSolverFamily(const std::string & value, SolverFamily & family);
} // namespace aipackaging::solver

#endif
