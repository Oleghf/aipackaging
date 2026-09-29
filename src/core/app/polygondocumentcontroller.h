#ifndef AIPACKAGING_APPLICATION_POLYGONDOCUMENTCONTROLLER_H
#define AIPACKAGING_APPLICATION_POLYGONDOCUMENTCONTROLLER_H

#include <memory>
#include <optional>
#include <string>

#include <aipackaging/editor/polygon_editor_commands.h>
#include <polygoneditablecontracts.h>
#include <polygonworkspaceports.h>

class PolygonWorkspaceController;

/// Владеет редактируемой моделью и жизненным циклом одного активного документа.
class PolygonDocumentController : public std::enable_shared_from_this<PolygonDocumentController>
{
public:
  /// Связывает модель документа с файловым шлюзом, рабочей областью и путём автосохранения.
  PolygonDocumentController(std::shared_ptr<IPolygonEditableDocumentGateway> gateway,
                            std::shared_ptr<PolygonWorkspaceController> workspace,
                            std::shared_ptr<ActivePolygonDocument> activeDocument, std::string autosavePath);
  /// Завершает контроллер, оставляя автоматический черновик доступным следующему запуску.
  ~PolygonDocumentController();
  /// Подменяет документные действия совместимого набора функциями этого контроллера.
  void bindActions(PolygonWorkspaceActions & actions);
  /// Проверяет наличие автоматического черновика для стартовой страницы.
  void inspectRecovery();
  /// Открывает задачу или пользовательский черновик после завершения активного поиска.
  void openDocument(const std::string & filePath);
  /// Создаёт новый грязный документ с листом в миллиметрах и пустым составом деталей.
  void createDocument(const std::string & problemId, double sheetWidth, double sheetHeight);
  /// Сохраняет активный документ как строгую задачу либо пользовательский черновик.
  void saveDocument(const std::string & filePath, bool asDraft);
  /// Немедленно сохраняет актуальное поколение грязного документа в автоматический черновик.
  void autosave();
  /// Восстанавливает найденный автоматический черновик как грязный документ без исходного пути сохранения.
  void restoreRecovery();
  /// Удаляет найденный автоматический черновик по подтверждённому действию пользователя.
  void deleteRecovery();
  /// Принимает построенный импортом документ после согласованного завершения поиска.
  void adoptImported(PolygonEditableDocumentLoadResult loaded);
  /// Выполняет атомарный пакет изменений, затем повторяет точную проверку снимка.
  void editDocument(const aipackaging::editor::EditorCommandBatch & batch);
  /// Отменяет последнюю транзакцию документа.
  void undo();
  /// Повторяет следующую отменённую транзакцию документа.
  void redo();
  /// Начинает непрерывное изменение и возвращает его идентификатор либо ноль при блокировке.
  std::uint64_t beginGesture() noexcept;
  /// Отменяет незавершённое объединённое изменение.
  void cancelGesture(std::uint64_t gestureId);
  /// Завершает объединение изменений одного жеста.
  void finishGesture(std::uint64_t gestureId);

private:
  /// Выполняет загрузку после согласованного завершения предыдущей фоновой работы.
  void loadAfterStop(const std::string & filePath, bool recovery);
  /// Публикует новый документ только после полного успеха чтения.
  void acceptLoaded(PolygonEditableDocumentLoadResult loaded, bool recovered);
  /// Удаляет автоматический черновик после успешного пользовательского сохранения.
  bool clearRecoveryAfterSave(const std::string & savedPath);
  /// Компилирует текущее состояние сессии и публикует его как изменение документа.
  void publishEdited(const aipackaging::editor::EditorCommandResult & result, aipackaging::editor::PolygonEditorSession previous);

  std::shared_ptr<IPolygonEditableDocumentGateway> gateway_;
  std::shared_ptr<PolygonWorkspaceController> workspace_;
  std::shared_ptr<ActivePolygonDocument> activeDocument_;
  std::string autosavePath_;
  std::optional<aipackaging::editor::PolygonEditorSession> session_;
  std::optional<PolygonSourceFingerprint> baseFingerprint_;
  std::uint64_t generation_ = 0;
};

#endif
