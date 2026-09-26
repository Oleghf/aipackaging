#ifndef AIPACKAGING_APPLICATION_POLYGONWORKSPACEPORTS_H
#define AIPACKAGING_APPLICATION_POLYGONWORKSPACEPORTS_H

#include <functional>
#include <optional>
#include <string>

#include <polygondocumentcontracts.h>
#include <polygonmodelcontracts.h>
#include <polygonruncontracts.h>

/// Набор действий полигональной вкладки, привязываемых в корне композиции.
struct PolygonWorkspaceActions
{
  std::function<void(const std::string &)> openProblem;
  std::function<void(const std::string &)> saveSolution;
  std::function<void(const std::string &)> openModel;
  std::function<void()> cancelModelLoad;
  std::function<void()> forgetModel;
  std::function<void(const NestingRunRequest &)> start;
  std::function<void()> cancel;
};

/// Выполняет раскрой вне прикладного потока и доставляет типизированные события.
class INestingJobRunner
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~INestingJobRunner() = default;
  /// Запускает одну работу и возвращает её идентификатор либо причину отказа.
  virtual std::optional<NestingJobHandle> start(PolygonDocumentHandle document, const NestingRunRequest & request,
                                                NestingJobCallbacks callbacks, std::string & error) = 0;
  /// Запрашивает отмену указанной активной работы без ожидания её завершения.
  virtual void cancel(NestingJobHandle job) noexcept = 0;
};

/// Принимает неизменяемые снимки полигонального пользовательского сценария.
class IPolygonWorkspaceOutput
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonWorkspaceOutput() = default;
  /// Показывает новый согласованный снимок состояния.
  virtual void presentPolygonWorkspace(const PolygonWorkspaceSnapshot & snapshot) = 0;
};

/// Ставит функции в очередь потока, которому принадлежит контроллер.
class IApplicationDispatcher
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IApplicationDispatcher() = default;
  /// Ставит функцию в очередь без синхронного вызова либо возвращает отказ без исключения.
  virtual bool post(std::function<void()> callback) noexcept = 0;
};

/// Управляет одним способом получения проверенного результата раскроя.
class IPolygonNestingBackend
{
public:
  /// Управление отменой и ходом одной операции внутренней реализации.
  struct Control
  {
    std::function<bool()> cancellationRequested;
    std::function<void(const NestingProgress &)> progress;
  };

  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonNestingBackend() = default;
  /// Выполняет запрос и возвращает только прикладные структуры результата.
  virtual NestingRunResult run(PolygonDocumentHandle document, const NestingRunRequest & request, const Control & control) = 0;
};

#endif
