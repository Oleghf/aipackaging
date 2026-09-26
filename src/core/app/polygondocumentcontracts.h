#ifndef AIPACKAGING_APPLICATION_POLYGONDOCUMENTCONTRACTS_H
#define AIPACKAGING_APPLICATION_POLYGONDOCUMENTCONTRACTS_H

#include <string>
#include <vector>

#include <polygonidentifiers.h>
#include <polygonworkspacepresentation.h>

/// Результат загрузки полигонального документа через прикладной порт.
struct PolygonDocumentLoadResult
{
  bool success = false;
  std::string error;
  PolygonDocumentHandle document;
  std::string problemId;
  PolygonDocumentSummary summary;
  PolygonSceneView scene;
  std::vector<std::string> unplacedInstances;
};

/// Результат файловой операции без исключений в пользовательском сценарии.
struct PolygonDocumentOperationResult
{
  bool success = false;
  std::string error;
};

/// Загружает и сохраняет документы, скрывая JSON и файловую систему.
class IPolygonDocumentGateway
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonDocumentGateway() = default;
  /// Загружает документ и возвращает представление только после полной проверки.
  virtual PolygonDocumentLoadResult load(const std::string & filePath) = 0;
  /// Сохраняет ранее проверенное решение в указанный файл.
  virtual PolygonDocumentOperationResult save(const std::string & filePath, PolygonSolutionHandle solution) = 0;
  /// Без исключений освобождает задачу; вызов разрешён из рабочего потока.
  virtual void release(PolygonDocumentHandle document) noexcept = 0;
  /// Без исключений освобождает решение; вызов разрешён из рабочего потока.
  virtual void release(PolygonSolutionHandle solution) noexcept = 0;
};

#endif
