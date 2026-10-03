#ifndef AIPACKAGING_APPLICATION_POLYGONWORKSPACECONTROLLER_H
#define AIPACKAGING_APPLICATION_POLYGONWORKSPACECONTROLLER_H

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <activepolygondocument.h>
#include <aipackaging/editor/polygon_editor_commands.h>
#include <polygoneditablecontracts.h>
#include <polygonworkspaceports.h>

/// Управляет полигональным пользовательским сценарием как однопоточный автомат состояния.
class PolygonWorkspaceController : public std::enable_shared_from_this<PolygonWorkspaceController>
{
public:
  /// Создаёт контроллер поверх прикладных портов документов, выполнения и вывода.
  PolygonWorkspaceController(std::shared_ptr<IPolygonWorkspaceOutput> output, std::shared_ptr<IPolygonDocumentGateway> documents,
                             std::shared_ptr<INestingJobRunner> jobs, std::shared_ptr<IPolygonModelJobRunner> modelJobs = {},
                             std::shared_ptr<ActivePolygonDocument> activeDocument = {});
  /// Освобождает документы и запрашивает отмену активной работы.
  ~PolygonWorkspaceController();
  /// Формирует безопасные слабые обработчики действий для интерфейса.
  PolygonWorkspaceActions actions();
  /// Загружает документ, сохраняя прежнюю сцену при ошибке.
  void openProblem(const std::string & filePath);
  /// Сохраняет последнее разрешённое и проверенное решение.
  void saveSolution(const std::string & filePath);
  /// Запускает фоновую проверку комплекта модели, сохраняя прежний при ошибке.
  void openModel(const std::string & directory);
  /// Запрашивает отмену текущей проверки модели.
  void cancelModelLoad();
  /// Забывает текущую проверенную модель, если она не используется поиском.
  void forgetModel();
  /// Запускает выбранный способ раскроя, если задача готова.
  void start(const NestingRunRequest & request);
  /// Запрашивает отмену текущей работы без блокировки вызывающего потока.
  void cancel();
  /// Возвращает последний опубликованный снимок модели представления.
  PolygonWorkspaceSnapshot snapshot() const;
  /// Отменяет текущий поиск и выполняет замену документа только после итогового события.
  void requestDocumentReplacement(std::function<void()> replacement);
  /// Публикует успешно загруженный редактируемый документ и его необязательный точный снимок.
  void acceptEditableDocument(PolygonEditableDocumentLoadResult loaded, bool dirty,
                              std::shared_ptr<const aipackaging::editor::EditablePolygonDocument> document,
                              const aipackaging::editor::EditorHistoryState & history);
  /// Публикует изменённый документ, не удаляя прежнее решение из режима просмотра.
  void acceptEditedDocument(PolygonEditableDocumentLoadResult loaded, const aipackaging::editor::EditorHistoryState & history,
                            std::shared_ptr<const aipackaging::editor::EditablePolygonDocument> document);
  /// Обновляет доступность отмены и повтора после сохранения без замены документа.
  void presentEditorHistory(std::shared_ptr<const aipackaging::editor::EditablePolygonDocument> document,
                            const aipackaging::editor::EditorHistoryState & history);
  /// Публикует состояние найденного автоматического черновика.
  void presentRecovery(PolygonRecoveryCandidate recovery);
  /// Показывает диагностируемый результат операции документа без потери прежней сцены.
  void reportDocumentOperation(std::string message);
  /// Публикует изменения признаков активного документа после сохранения.
  void refreshDocumentState();

private:
  std::uint64_t nextEditableDocumentIdentity_ = 0;
  /// Публикует состояние и вычисляет доступность действий.
  void publish();
  /// Формирует сведения документа и доступность истории из исходного состояния владельцев.
  void projectDocumentState();
  /// Принимает сообщение о ходе актуальной работы.
  void acceptProgress(NestingJobHandle job, const NestingProgress & progress);
  /// Принимает подготовленный результат актуальной работы.
  void acceptResult(NestingJobHandle job, NestingRunResult result);
  /// Завершает актуальную работу диагностируемой ошибкой.
  void acceptFailure(NestingJobHandle job, const std::string & error);
  /// Принимает проверенный комплект только от актуальной фоновой работы.
  void acceptModel(PolygonModelJobHandle job, PolygonModelLoadResult result);
  /// Завершает актуальную проверку модели диагностируемой ошибкой.
  void acceptModelFailure(PolygonModelJobHandle job, const std::string & error);
  /// Завершает актуальную проверку модели после отмены.
  void acceptModelCancellation(PolygonModelJobHandle job);
  /// Освобождает текущий сохраняемый результат.
  void releaseSolution() noexcept;
  /// Освобождает текущий комплект модели.
  void releaseModel() noexcept;
  /// Выполняет отложенную замену после полного завершения фонового поиска.
  void continueDocumentReplacement();

  std::shared_ptr<IPolygonWorkspaceOutput> output_;
  std::shared_ptr<IPolygonDocumentGateway> documents_;
  std::shared_ptr<INestingJobRunner> jobs_;
  std::shared_ptr<IPolygonModelJobRunner> modelJobs_;
  std::shared_ptr<ActivePolygonDocument> document_;
  std::optional<PolygonSolutionHandle> solution_;
  std::optional<PolygonModelHandle> model_;
  std::optional<PolygonModelJobHandle> activeModelJob_;
  std::optional<NestingJobHandle> activeJob_;
  std::function<void()> pendingDocumentReplacement_;
  PolygonWorkspaceSnapshot snapshot_;
  aipackaging::editor::EditorHistoryState editorHistory_;
};

#endif
