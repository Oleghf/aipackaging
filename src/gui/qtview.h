#ifndef QTVIEW_H__
#define QTVIEW_H__

#include <QMainWindow>
#include <string>
#include <vector>

#include <iview.h>
#include <polygonworkspaceview.h>


struct Point2D;
class PackagingWidget;
class PolygonWorkspaceWidget;
class QTabWidget;
class Event;
class QResizeEvent;
class ToastNotification;

////////////////////////////////////////////////////////////////////////////////
//
/// Представление приложения
/**
*/
////////////////////////////////////////////////////////////////////////////////
class QtView : public QMainWindow,
               public IView,
               public IPolygonWorkspaceView
{
  Q_OBJECT
public:
  QtView();

  // Включить/Отключить действие
  void setActionEnabled(PackagingAction action, bool isEnabled) override;
  bool isActionEnabled(PackagingAction action) const override;

  // Диалоги
  std::string openSaveFileDialog(const std::string & title, const std::string & initPath, const std::string & filter) override;
  std::string openLoadFileDialog(const std::string & title, const std::string & initPath, const std::string & filter) override;
  void showMessage(const std::string & title, const std::string & message, MessageType type) override;

  // Запросить перерисовку области
  void requestRedraw() override;

  // Добавить/Удалить слушателей ивентов
  void addEventListener(std::shared_ptr<EventListener> listener) override;
  void removeEventListener(std::shared_ptr<EventListener> listener) override;

  // Зум
  void setZoomFactor(double factor) override;
  double zoomFactor() const override;

  void statisticChangeCountAllCells(unsigned int allCells) override;
  void statisticChangeCountOccupiedCells(unsigned int occupiedCells) override;

  /// Устанавливает обработчики действий полигональной вкладки.
  void setPolygonWorkspaceActions(PolygonWorkspaceActions actions) override;
  /// Передаёт presentation-снимок полигональному виджету.
  void presentPolygonWorkspace(const PolygonWorkspaceSnapshot & snapshot) override;
  /// Ставит callback в очередь главного Qt-потока.
  void postToPolygonUi(std::function<void()> callback) override;

private slots:
  // Получает объект рисования с основной сцены
  void sendMainSceneQPainter(QPainter & painter);
  // Получает объект рисования со сцены генерации
  void sendGenerateSceneQPainter(QPainter & painter);
  // Получает событие нажатия мыши с основной сцены
  void sendMainSceneMouseEvent(QMouseEvent * event);

private slots:
  // Слоты связаны с действиями для генерации ивентов
  void generateOpenSceneFileEvent();
  void generateSaveFileEvent();
  void generateLoadFileEvent();
  void generateUndoEvent();
  void generateRedoEvent();
  void generateChangeStateEvent();

private:
  void keyPressEvent(QKeyEvent * event) override;
  void resizeEvent(QResizeEvent * event) override;

private:
  // Отправить ивент всем слушателям
  void sendEvent(const Event & event);
  // Разместить всплывающее сообщение
  void positionToast();

private:
  std::vector<std::shared_ptr<EventListener>> listeners_;

  PackagingWidget * packaging_;
  PolygonWorkspaceWidget * polygonWorkspace_;
  QTabWidget * workspaceTabs_;
  PolygonWorkspaceActions polygonActions_;

  // Действия в редакторе
  QAction * open_;
  QAction * loadPool_;
  QAction * save_;
  QAction * saveAs_;

  QAction * undo_;
  QAction * redo_;

  QAction * changeMode_;

  // Модификатор для зума
  double zoomFactor_;

  // Активное всплывающее сообщение
  ToastNotification * activeToast_ = nullptr;
};

#endif
