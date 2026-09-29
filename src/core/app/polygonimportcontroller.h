#ifndef AIPACKAGING_APPLICATION_POLYGONIMPORTCONTROLLER_H
#define AIPACKAGING_APPLICATION_POLYGONIMPORTCONTROLLER_H

#include <memory>
#include <optional>

#include <polygonimportcontracts.h>

class PolygonDocumentController;

/// Управляет одним фоновым сеансом импорта, не раскрывая приложению формат DXF.
class PolygonImportController : public std::enable_shared_from_this<PolygonImportController>
{
public:
  /// Связывает вывод мастера, средство фоновой работы и владелец документа.
  PolygonImportController(std::shared_ptr<IPolygonImportOutput> output, std::shared_ptr<IPolygonImportJobRunner> jobs,
                          std::shared_ptr<PolygonDocumentController> documents);
  /// Отменяет активную работу и освобождает непринятые результаты.
  ~PolygonImportController();
  /// Формирует слабые обработчики действий для Qt-мастера.
  PolygonImportActions actions();
  /// Возвращает последний опубликованный снимок мастера.
  PolygonImportSnapshot snapshot() const;

private:
  /// Начинает анализ нового файла после освобождения прежнего сеанса.
  void inspect(const PolygonImportInspectionRequest & request);
  /// Строит документ по текущему сеансу и подтверждённым настройкам.
  void build(const PolygonImportConfiguration & configuration);
  /// Запрашивает отмену актуальной фоновой работы.
  void cancel();
  /// Передаёт построенный документ основному контроллеру либо оставляет его черновиком.
  void accept(bool asDraft);
  /// Закрывает сеанс без изменения активного документа.
  void close();
  /// Принимает только результат актуального анализа.
  bool acceptInspection(PolygonImportJobHandle job, PolygonImportInspection inspection);
  /// Принимает только результат актуального построения.
  bool acceptBuild(PolygonImportJobHandle job, PolygonImportBuildResult result);
  /// Завершает актуальную работу диагностируемой ошибкой.
  void acceptFailure(PolygonImportJobHandle job, const std::string & error);
  /// Завершает актуальную работу после отмены.
  void acceptCancellation(PolygonImportJobHandle job);
  /// Освобождает все временные идентификаторы, которыми ещё владеет мастер.
  void releaseOwned() noexcept;
  /// Публикует согласованный снимок и доступность команд.
  void publish();

  std::shared_ptr<IPolygonImportOutput> output_;
  std::shared_ptr<IPolygonImportJobRunner> jobs_;
  std::shared_ptr<PolygonDocumentController> documents_;
  std::optional<PolygonImportJobHandle> activeJob_;
  std::optional<PolygonImportSessionHandle> session_;
  std::optional<PolygonImportBuildResult> result_;
  PolygonImportSnapshot snapshot_;
};

#endif
