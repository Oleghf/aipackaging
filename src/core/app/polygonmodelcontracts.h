#ifndef AIPACKAGING_APPLICATION_POLYGONMODELCONTRACTS_H
#define AIPACKAGING_APPLICATION_POLYGONMODELCONTRACTS_H

#include <functional>
#include <optional>
#include <string>

#include <polygonidentifiers.h>

/// Результат строгой загрузки внешнего комплекта полигональной модели.
struct PolygonModelLoadResult
{
  bool success = false;
  std::string error;
  PolygonModelHandle model;
  std::string modelId;
  std::string modelSha256;
};

/// Загружает и освобождает внешние комплекты модели без раскрытия ONNX Runtime.
class IPolygonModelGateway
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonModelGateway() = default;
  /// Проверяет комплект модели и возвращает процессный идентификатор.
  virtual PolygonModelLoadResult load(const std::string & directory) = 0;
  /// Освобождает больше не используемую модель текущего процесса.
  virtual void release(PolygonModelHandle model) noexcept = 0;
};

/// Набор функций для завершения фоновой проверки комплекта модели.
struct PolygonModelJobCallbacks
{
  std::function<void(PolygonModelJobHandle, PolygonModelLoadResult)> completed;
  std::function<void(PolygonModelJobHandle, const std::string &)> failed;
  std::function<void(PolygonModelJobHandle)> cancelled;
};

/// Выполняет проверку модели вне прикладного потока и владеет её отменой.
class IPolygonModelJobRunner
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonModelJobRunner() = default;
  /// Запускает проверку каталога либо возвращает причину отказа.
  virtual std::optional<PolygonModelJobHandle> start(const std::string & directory, PolygonModelJobCallbacks callbacks,
                                                     std::string & error) = 0;
  /// Запрашивает отмену совпадающей фоновой проверки.
  virtual void cancel(PolygonModelJobHandle job) noexcept = 0;
  /// Освобождает больше не используемую проверенную модель.
  virtual void release(PolygonModelHandle model) noexcept = 0;
};

#endif
