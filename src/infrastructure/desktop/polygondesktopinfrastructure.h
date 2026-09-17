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

private:
  /// Закрытая реализация скрывает геометрию и контейнеры от корня композиции.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

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
