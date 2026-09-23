#ifndef AIPACKAGING_APPLICATION_POLYGONMODELCONTRACTS_H
#define AIPACKAGING_APPLICATION_POLYGONMODELCONTRACTS_H

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

#endif
