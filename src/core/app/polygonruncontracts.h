#ifndef AIPACKAGING_APPLICATION_POLYGONRUNCONTRACTS_H
#define AIPACKAGING_APPLICATION_POLYGONRUNCONTRACTS_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <polygonidentifiers.h>
#include <polygonworkspacepresentation.h>

/// Выбирает семейство внутренней реализации раскроя.
enum class NestingMethod : std::uint8_t
{
  Baseline,
  Neural,
  Hybrid
};

/// Выбирает один из пяти базовых алгоритмов.
enum class BaselineAlgorithm : std::uint8_t
{
  InputFirstFit,
  AreaLeftBottom,
  MaxSideLeftBottom,
  RandomLeftBottom,
  Beam
};

/// Выбирает жадный или многократный способ применения нейросетевой политики.
enum class NeuralSelectionMode : std::uint8_t
{
  Greedy,
  BestOf
};

/// Задаёт параметры одного запуска без раскрытия типов поискового модуля.
struct NestingRunRequest
{
  NestingMethod method = NestingMethod::Baseline;
  BaselineAlgorithm algorithm = BaselineAlgorithm::AreaLeftBottom;
  NeuralSelectionMode neuralSelection = NeuralSelectionMode::BestOf;
  std::optional<PolygonModelHandle> model;
  std::uint64_t seed = 42;
  std::size_t randomIterations = 64;
  std::size_t beamWidth = 32;
  std::size_t maxExpandedStates = 50'000;
  std::uint64_t timeoutMs = 30'000;
  std::size_t neuralRollouts = 16;
  std::size_t fallbackRandomIterations = 64;
};

/// Обозначает причину завершения раскроя.
enum class NestingCompletion : std::uint8_t
{
  Solved,
  NoSolutionFound,
  BudgetExhausted,
  TimedOut,
  UnsupportedEnvironment,
  Cancelled
};

/// Обозначает фактически использованный источник результата.
enum class NestingProvenance : std::uint8_t
{
  Baseline,
  Neural,
  Hybrid,
  HybridFallback
};

/// Описывает результат, уже подготовленный и проверенный инфраструктурой.
struct NestingRunResult
{
  NestingCompletion completion = NestingCompletion::NoSolutionFound;
  NestingProvenance provenance = NestingProvenance::Baseline;
  std::string implementationName;
  std::string diagnostic;
  std::string solutionStatus;
  bool partial = true;
  NestingObjectiveSummary objective;
  NestingMetricsSummary metrics;
  PolygonSceneView scene;
  std::vector<std::string> unplacedInstances;
  std::optional<PolygonSolutionHandle> solution;
};

/// Набор функций для сообщений о ходе и результате работы.
struct NestingJobCallbacks
{
  std::function<void(NestingJobHandle, const NestingProgress &)> progress;
  /// Возвращает `true`, если получатель принял владение проверенным решением.
  std::function<bool(NestingJobHandle, NestingRunResult)> completed;
  std::function<void(NestingJobHandle, const std::string &)> failed;
};

#endif
