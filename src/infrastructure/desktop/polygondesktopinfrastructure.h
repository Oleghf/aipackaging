#ifndef AIPACKAGING_INFRASTRUCTURE_POLYGONDESKTOPINFRASTRUCTURE_H
#define AIPACKAGING_INFRASTRUCTURE_POLYGONDESKTOPINFRASTRUCTURE_H

#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <polygonworkspaceview.h>

namespace aipackaging::solver
{
class PolygonEnvironment;
struct PolygonProblem;
struct PolygonSolution;
} // namespace aipackaging::solver

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
namespace aipackaging::inference
{
class PolygonOnnxPolicy;
}
#endif

/// Потокобезопасное хранилище задач и проверенных решений текущего процесса.
class PolygonArtifactStore
{
public:
  /// Неизменяемая закрытая запись задачи и подготовленной геометрии.
  struct DocumentRecord;

  /// Создаёт пустое процессное хранилище.
  PolygonArtifactStore();
  /// Уничтожает закрытое содержимое после освобождения всех разделяемых записей.
  ~PolygonArtifactStore();

  /// Регистрирует проверенную задачу и возвращает новый процессный идентификатор.
  PolygonDocumentHandle addDocument(aipackaging::solver::PolygonProblem problem,
                                    std::shared_ptr<aipackaging::solver::PolygonEnvironment> environment);
  /// Возвращает зарегистрированную задачу либо пустой указатель.
  std::shared_ptr<const DocumentRecord> document(PolygonDocumentHandle handle) const;
  /// Проверяет и регистрирует решение соответствующей задачи.
  std::optional<PolygonSolutionHandle> addValidatedSolution(PolygonDocumentHandle document,
                                                            aipackaging::solver::PolygonSolution solution, std::string & error);
  /// Возвращает зарегистрированное проверенное решение либо пустой указатель.
  std::shared_ptr<const aipackaging::solver::PolygonSolution> solution(PolygonSolutionHandle handle) const;
  /// Удаляет задачу из хранилища.
  void release(PolygonDocumentHandle handle) noexcept;
  /// Удаляет решение из хранилища.
  void release(PolygonSolutionHandle handle) noexcept;
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
  /// Регистрирует проверенный комплект модели и возвращает процессный идентификатор.
  PolygonModelHandle addModel(std::shared_ptr<aipackaging::inference::PolygonOnnxPolicy> model);
  /// Возвращает зарегистрированную модель либо пустой указатель.
  std::shared_ptr<const aipackaging::inference::PolygonOnnxPolicy> model(PolygonModelHandle handle) const;
  /// Удаляет модель из процессного хранилища.
  void release(PolygonModelHandle handle) noexcept;
#endif

private:
  /// Закрытая реализация скрывает геометрию и контейнеры от корня композиции.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
/// Загружает внешний комплект ONNX и регистрирует только полностью проверенную модель.
class LocalPolygonModelGateway final : public IPolygonModelGateway
{
public:
  /// Связывает шлюз модели с общим процессным хранилищем.
  explicit LocalPolygonModelGateway(std::shared_ptr<PolygonArtifactStore> store);
  /// Проверяет каталог модели и возвращает её идентичность.
  PolygonModelLoadResult load(const std::string & directory) override;
  /// Освобождает зарегистрированную модель.
  void release(PolygonModelHandle model) noexcept override;

private:
  std::shared_ptr<PolygonArtifactStore> store_;
};

/// Проверяет комплект модели в отдельном потоке и доставляет события приложению.
class StdThreadPolygonModelJobRunner final : public IPolygonModelJobRunner
{
public:
  /// Сохраняет шлюз модели и диспетчер потока приложения.
  StdThreadPolygonModelJobRunner(std::shared_ptr<IPolygonModelGateway> gateway,
                                 std::shared_ptr<IApplicationDispatcher> dispatcher);
  /// Запрашивает остановку и присоединяет рабочий поток.
  ~StdThreadPolygonModelJobRunner() override;
  /// Запускает одну проверку, если другая проверка не выполняется.
  std::optional<PolygonModelJobHandle> start(const std::string & directory, PolygonModelJobCallbacks callbacks,
                                             std::string & error) override;
  /// Запрашивает остановку совпадающей проверки.
  void cancel(PolygonModelJobHandle job) noexcept override;
  /// Передаёт освобождение модели нижележащему шлюзу.
  void release(PolygonModelHandle model) noexcept override;

private:
  std::shared_ptr<IPolygonModelGateway> gateway_;
  std::shared_ptr<IApplicationDispatcher> dispatcher_;
  std::mutex mutex_;
  std::jthread worker_;
  std::uint64_t nextJob_ = 1;
  std::optional<PolygonModelJobHandle> activeJob_;
};
#endif

/// Загружает и сохраняет полигональные JSON-документы через локальную файловую систему.
class LocalPolygonDocumentGateway final : public IPolygonDocumentGateway
{
public:
  /// Связывает шлюз с общим процессным хранилищем артефактов.
  explicit LocalPolygonDocumentGateway(std::shared_ptr<PolygonArtifactStore> store);
  /// Загружает, нормализует и регистрирует задачу.
  PolygonDocumentLoadResult load(const std::string & filePath) override;
  /// Сохраняет зарегистрированное проверенное решение.
  PolygonDocumentOperationResult save(const std::string & filePath, PolygonSolutionHandle solution) override;
  /// Освобождает зарегистрированную задачу.
  void release(PolygonDocumentHandle document) noexcept override;
  /// Освобождает зарегистрированное решение.
  void release(PolygonSolutionHandle solution) noexcept override;

private:
  std::shared_ptr<PolygonArtifactStore> store_;
};

/// Выполняет пять полигональных базовых алгоритмов и формирует проверенный результат.
class BaselinePolygonBackend final : public IPolygonNestingBackend
{
public:
  /// Связывает внутреннюю реализацию с процессным хранилищем задач и решений.
  explicit BaselinePolygonBackend(std::shared_ptr<PolygonArtifactStore> store);
  /// Запускает выбранный базовый алгоритм и повторно проверяет полученное решение.
  NestingRunResult run(PolygonDocumentHandle document, const NestingRunRequest & request, const Control & control) override;

private:
  std::shared_ptr<PolygonArtifactStore> store_;
};

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
/// Выполняет нейросетевой и гибридный раскрой над проверенным комплектом ONNX.
class OnnxPolygonBackend final : public IPolygonNestingBackend
{
public:
  /// Связывает внутреннюю реализацию с хранилищем задач, моделей и решений.
  explicit OnnxPolygonBackend(std::shared_ptr<PolygonArtifactStore> store);
  /// Выполняет выбранный нейросетевой режим и проверяет итоговое решение.
  NestingRunResult run(PolygonDocumentHandle document, const NestingRunRequest & request, const Control & control) override;

private:
  std::shared_ptr<PolygonArtifactStore> store_;
};
#endif

/// Направляет прикладной запрос в базовую или доступную нейросетевую реализацию.
class PolygonBackendRouter final : public IPolygonNestingBackend
{
public:
  /// Сохраняет обязательную базовую и необязательную нейросетевую реализации.
  PolygonBackendRouter(std::shared_ptr<IPolygonNestingBackend> baseline, std::shared_ptr<IPolygonNestingBackend> neural = {});
  /// Выбирает реализацию только по явно указанному методу запроса.
  NestingRunResult run(PolygonDocumentHandle document, const NestingRunRequest & request, const Control & control) override;

private:
  std::shared_ptr<IPolygonNestingBackend> baseline_;
  std::shared_ptr<IPolygonNestingBackend> neural_;
};

/// Выполняет внутреннюю реализацию в одном рабочем потоке и доставляет события через диспетчер.
class StdThreadNestingJobRunner final : public INestingJobRunner
{
public:
  /// Сохраняет внутреннюю реализацию и диспетчер потока приложения.
  StdThreadNestingJobRunner(std::shared_ptr<IPolygonNestingBackend> backend, std::shared_ptr<IApplicationDispatcher> dispatcher);
  /// Запрашивает остановку и дожидается завершения рабочего потока.
  ~StdThreadNestingJobRunner() override;
  /// Запускает работу, если другая работа не выполняется.
  std::optional<NestingJobHandle> start(PolygonDocumentHandle document, const NestingRunRequest & request,
                                        NestingJobCallbacks callbacks, std::string & error) override;
  /// Запрашивает остановку совпадающей активной работы.
  void cancel(NestingJobHandle job) noexcept override;

private:
  std::shared_ptr<IPolygonNestingBackend> backend_;
  std::shared_ptr<IApplicationDispatcher> dispatcher_;
  std::mutex mutex_;
  std::jthread worker_;
  std::uint64_t nextJob_ = 1;
  std::optional<NestingJobHandle> activeJob_;
};

#endif
