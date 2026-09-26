#ifndef AIPACKAGING_INFERENCE_POLYGON_ONNX_H
#define AIPACKAGING_INFERENCE_POLYGON_ONNX_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <aipackaging/nesting/polygon_types.h>
#include <aipackaging/nesting/search_contracts.h>

namespace aipackaging::inference
{
/// Описывает проверенный внешний комплект полигональной модели ONNX.
struct PolygonModelMetadata
{
  std::string modelId;
  std::string modelSha256;
  std::string checkpointSha256;
  std::string datasetManifestSha256;
  std::size_t hiddenSize = 0;
  std::size_t maxInstances = 0;
  std::size_t maxCandidatesPerPair = 0;
};

/// Выбирает жадный либо многократный способ получения нейросетевого решения.
enum class PolygonPolicyMode : std::uint8_t
{
  Greedy,
  BestOf
};

/// Задаёт воспроизводимые параметры одного запуска полигональной модели.
struct PolygonPolicyConfig
{
  PolygonPolicyMode mode = PolygonPolicyMode::Greedy;
  std::uint64_t seed = 42;
  std::size_t rollouts = 16;
  std::uint64_t timeoutMs = 300000;
};

/// Передаёт отмену и ход выполнения синхронному исполнителю модели.
struct PolygonPolicyControl
{
  std::function<bool()> cancellationRequested;
  std::function<void(const aipackaging::solver::SearchProgress &)> baselineProgress;
  std::function<void(std::size_t completed, std::size_t total)> progress;
};

/// Возвращает лучшее проверяемое решение и причину досрочной остановки.
struct PolygonPolicyExecutionResult
{
  aipackaging::solver::PolygonSolution solution;
  bool cancelled = false;
  bool fallbackUsed = false;
  std::string warning;
};

/// Загружает комплект ONNX и получает решения только через каталог точной среды.
class PolygonOnnxPolicy
{
public:
  /// Проверяет метаданные, SHA-256 и графы либо возвращает диагностическую ошибку.
  static std::shared_ptr<PolygonOnnxPolicy> Load(const std::string & directory, std::string & error);

  /// Освобождает сеансы ONNX Runtime после завершения активных вызовов.
  ~PolygonOnnxPolicy();
  /// Возвращает проверенную идентичность загруженного комплекта.
  const PolygonModelMetadata & metadata() const noexcept;
  /// Выполняет жадный или многократный запуск над нормализованной задачей.
  PolygonPolicyExecutionResult run(const aipackaging::solver::PolygonProblem & problem, const PolygonPolicyConfig & config,
                                   const PolygonPolicyControl & control = {}) const;
  /// Запускает базовый алгоритм первым и публикует лучшее проверенное решение.
  PolygonPolicyExecutionResult runHybrid(const aipackaging::solver::PolygonProblem & problem,
                                         const PolygonPolicyConfig & policyConfig,
                                         const aipackaging::solver::SolverConfig & fallbackConfig,
                                         const PolygonPolicyControl & control = {}) const;

  /// Запрещает копирование владельца сеансов ONNX Runtime.
  PolygonOnnxPolicy(const PolygonOnnxPolicy &) = delete;
  /// Запрещает присваивание владельца сеансов ONNX Runtime.
  PolygonOnnxPolicy & operator=(const PolygonOnnxPolicy &) = delete;

private:
  /// Принимает полностью построенную закрытую реализацию двух сеансов.
  explicit PolygonOnnxPolicy(std::unique_ptr<class PolygonOnnxPolicyImpl> impl);

  std::unique_ptr<class PolygonOnnxPolicyImpl> impl_;
};
} // namespace aipackaging::inference

#endif
