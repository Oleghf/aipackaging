#ifndef AIPACKAGING_GUI_POLYGONCANVASWIDGET_H
#define AIPACKAGING_GUI_POLYGONCANVASWIDGET_H

#include <cstdint>
#include <optional>
#include <QElapsedTimer>
#include <QPointF>
#include <QWidget>
#include <string>
#include <vector>

#include <polygoneditorcanvasinteraction.h>
#include <polygonworkspaceview.h>

class QFocusEvent;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPaintEvent;
class QWheelEvent;

/// Показывает исходную геометрию или раскладку и преобразует жесты редактора в прикладные команды.
class PolygonCanvasWidget : public QWidget
{
  Q_OBJECT
public:
  /// Создаёт полотно с автоматическим вписыванием листа и поддержкой клавиатурного фокуса.
  explicit PolygonCanvasWidget(QWidget * parent = nullptr);
  /// Заменяет отображаемый снимок, сохраняя только существующие выбранные сущности.
  void setSnapshot(const PolygonWorkspaceSnapshot & snapshot);
  /// Передаёт существующие прикладные действия редактора без введения нового порта.
  void setEditorActions(PolygonWorkspaceActions actions);
  /// Выделяет одну исходную сущность для совместимости с числовой панелью.
  void selectEditorEntity(std::uint64_t entityId);
  /// Выделяет устойчивый набор исходных сущностей.
  void selectEditorEntities(const std::vector<std::uint64_t> & entityIds);
  /// Переключает исходную задачу и результат без изменения документа.
  void setCanvasMode(PolygonCanvasMode mode);
  /// Переключает активный инструмент и отменяет незавершённый предварительный ввод.
  void setEditorTool(PolygonCanvasTool tool);
  /// Применяет пользовательские параметры сетки и привязок.
  void setSnapSettings(const PolygonCanvasSnapSettings & settings);
  /// Задаёт направление следующей интерактивной дуги.
  void setArcClockwise(bool clockwise) noexcept;
  /// Отменяет незавершённый жест перед запуском, заменой документа или закрытием.
  void cancelEditorInteraction();

  /// Сбрасывает пользовательский масштаб и смещение, чтобы вписать сцену в область просмотра.
  void fitToView();

signals:
  /// Сообщает координаты курсора в миллиметрах и нахождение внутри листа.
  void cursorPositionChanged(double x, double y, bool inside);
  /// Сообщает выбранный щелчком экземпляр размещённой детали.
  void partSelected(const QString & partId, std::uint32_t instanceIndex);
  /// Сообщает дереву редактора полный набор выбранных исходных сущностей.
  void editorEntitiesSelected(const std::vector<std::uint64_t> & entityIds);
  /// Просит панель инструментов согласованно сменить активный инструмент.
  void editorToolChangeRequested(PolygonCanvasTool tool);
  /// Показывает пользователю причину невозможности завершить интерактивную операцию.
  void editorInteractionMessage(const QString & message);

protected:
  /// Отменяет непрерывный жест, если другой объект отобрал захват мыши.
  bool event(QEvent * event) override;
  /// Рисует лист, сетку, выбранный режим, предварительную геометрию и маркеры редактора.
  void paintEvent(QPaintEvent * event) override;
  /// Начинает навигацию, выбор, перетаскивание либо очередную стадию построения.
  void mousePressEvent(QMouseEvent * event) override;
  /// Обновляет камеру, рамку выбора, предварительный просмотр или непрерывный жест.
  void mouseMoveEvent(QMouseEvent * event) override;
  /// Завершает навигацию, рамочный выбор или транзакцию перетаскивания.
  void mouseReleaseEvent(QMouseEvent * event) override;
  /// Масштабирует сцену относительно указателя.
  void wheelEvent(QWheelEvent * event) override;
  /// Включает временную навигацию пробелом и обрабатывает отмену через `Esc`.
  void keyPressEvent(QKeyEvent * event) override;
  /// Выключает временную навигацию после отпускания пробела.
  void keyReleaseEvent(QKeyEvent * event) override;
  /// Отменяет незавершённое перетаскивание при потере клавиатурного фокуса.
  void focusOutEvent(QFocusEvent * event) override;

private:
  /// Настраивает преобразование миллиметровых координат листа в экранные координаты.
  void applySceneTransform(QPainter & painter, const PolygonSceneView & scene) const;
  /// Рисует границу и материал листа.
  void drawSheet(QPainter & painter, const PolygonSceneView & scene) const;
  /// Рисует адаптивную сетку только в режиме исходной задачи.
  void drawGrid(QPainter & painter, const PolygonSceneView & scene) const;
  /// Рисует полезный правый остаток только для результата.
  void drawRemnant(QPainter & painter, const PolygonSceneView & scene) const;
  /// Рисует производственный отступ листа пунктирной линией.
  void drawMargin(QPainter & painter, const PolygonSceneView & scene) const;
  /// Рисует размещения, отверстия и выделение выбранной детали.
  void drawPlacements(QPainter & painter, const PolygonSceneView & scene) const;
  /// Рисует исходные цепочки, управляющие точки, направление и диагностики.
  void drawEditableDocument(QPainter & painter) const;
  /// Рисует рамку выбора, привязку и незавершённый сегмент.
  void drawInteractionOverlay(QPainter & painter) const;
  /// Переводит экранную точку в миллиметровые координаты листа.
  QPointF mapToSheet(const QPointF & position) const;
  /// Переводит миллиметровую точку в экранные координаты текущей камеры.
  QPointF mapFromSheet(const QPointF & point) const;
  /// Возвращает текущий экранный масштаб одного миллиметра.
  double sceneScale() const;
  /// Находит верхнюю размещённую деталь под точкой с учётом отверстий.
  const PolygonPlacedPartView * hitTestPlacement(const QPointF & sheetPoint) const;
  /// Возвращает выбранный открытый контур либо отсутствие.
  const aipackaging::editor::EditablePath * activeOpenPath() const;
  /// Применяет привязку к текущей координате с учётом клавиши `Alt`.
  PolygonCanvasSnapResult snapped(const QPointF & raw, Qt::KeyboardModifiers modifiers,
                                  aipackaging::editor::EntityId closingVertex = {}) const;
  /// Обновляет выбор и активную деталь, затем публикует его другим панелям.
  void publishSelection(const std::vector<aipackaging::editor::EntityId> & selection);
  /// Начинает непрерывный перенос выбранной геометрии.
  void beginDrag(const PolygonCanvasEntityHit & hit, const QPointF & sheetPoint);
  /// Публикует очередное либо итоговое положение непрерывного переноса.
  void updateDrag(const QPointF & sheetPoint, Qt::KeyboardModifiers modifiers, bool finalUpdate);
  /// Завершает или отменяет текущий жест редактора.
  void finishDrag(bool cancel);
  /// Выполняет очередной щелчок многостадийного инструмента рисования.
  void handleDrawingClick(const QPointF & sheetPoint, Qt::KeyboardModifiers modifiers);
  /// Создаёт первый узел внешнего контура или отверстия и начинает объединённый жест.
  void createPathAt(const QPointF & point, bool hole);
  /// Завершает готовый интерактивный сегмент и оставляет инструмент активным.
  void commitDrawingSegment(const QPointF & end, bool closePath);
  /// Находит созданный либо выбранный контур после синхронной публикации снимка.
  void refreshDrawingPath(bool hole);
  /// Сбрасывает только незавершённые стадии рисования.
  void clearDrawingPreview(bool cancelProvisionalPath);

  PolygonWorkspaceSnapshot snapshot_;
  PolygonWorkspaceActions editorActions_;
  PolygonCanvasMode mode_ = PolygonCanvasMode::Solution;
  PolygonCanvasTool tool_ = PolygonCanvasTool::Select;
  PolygonCanvasSnapSettings snapSettings_;
  bool arcClockwise_ = false;
  double zoom_ = 1.0;
  QPointF pan_;
  QPointF lastMousePosition_;
  bool panning_ = false;
  bool spacePressed_ = false;
  bool selectingRectangle_ = false;
  QPointF selectionStartScreen_;
  QPointF selectionCurrentScreen_;
  std::string selectedPartId_;
  std::uint32_t selectedInstanceIndex_ = 0;
  std::vector<aipackaging::editor::EntityId> selectedEditorEntities_;
  aipackaging::editor::EntityId activePart_;
  std::vector<aipackaging::editor::EntityId> diagnosticEntities_;
  bool dragging_ = false;
  std::uint64_t dragGesture_ = 0;
  QPointF dragStartSheet_;
  QPointF dragLastAppliedDelta_;
  QPointF dragPreviewDelta_;
  QPointF dragAnchor_;
  std::vector<aipackaging::editor::EntityId> dragPointIds_;
  QElapsedTimer dragPublishTimer_;
  aipackaging::editor::EntityId drawingPath_;
  aipackaging::editor::EntityId drawingPart_;
  std::optional<std::uint64_t> provisionalPathGesture_;
  std::vector<QPointF> drawingStagePoints_;
  QPointF drawingHover_;
  PolygonCanvasSnapResult currentSnap_;
  QString documentIdentity_;
};

#endif
