#include <algorithm>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPaintDevice>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QWidget>

#include <actionevent.h>
#include <changestateevent.h>
#include <event.h>
#include <loadfileevent.h>
#include <openscenefileevent.h>
#include <packagingwidget.h>
#include <painter.h>
#include <qtadapters.h>
#include <qtview.h>
#include <redoevent.h>
#include <savefileevent.h>
#include <scenemouseevent.h>
#include <scenepaintevent.h>
#include <scenewheelevent.h>
#include <scenewidget.h>
#include <toastnotification.h>
#include <undoevent.h>

namespace
{
constexpr int TOAST_MARGIN = 16;
constexpr int TOAST_DURATION_MS = 3000;
} // namespace


//------------------------------------------------------------------------------
/**
  \brief Конструктор
  \details Инициализирует виджеты, меню и связывает GUI-сигналы с application events
*/
//---
QtView::QtView()
  : QMainWindow(nullptr)
  , packaging_(new PackagingWidget(this))
  , zoomFactor_(1)
{
  QMenu * file = menuBar()->addMenu(tr("Файл"));
  QMenu * edit = menuBar()->addMenu(tr("Правка"));

  open_ = file->addAction(tr("Открыть сцену"));
  loadPool_ = file->addAction(tr("Загрузить пул"));
  save_ = file->addAction(tr("Сохранить сцену"));
  saveAs_ = file->addAction(tr("Сохранить сцену как"));

  undo_ = edit->addAction(tr("Отменить"));
  redo_ = edit->addAction(tr("Повторить"));
  changeMode_ = edit->addAction(tr("Изменить режим программы"));
  changeMode_->setVisible(false);

  open_->setShortcut(QKeySequence::Open);
  save_->setShortcut(QKeySequence::Save);
  saveAs_->setShortcut(QKeySequence::SaveAs);
  undo_->setShortcut(QKeySequence::Undo);
  redo_->setShortcut(QKeySequence::Redo);

  setCentralWidget(packaging_);

  connect(packaging_->mainScene(), &SceneWidget::sceneQPainterCreated, this, &QtView::sendMainSceneQPainter);
  connect(packaging_->generateScene(), &SceneWidget::sceneQPainterCreated, this, &QtView::sendGenerateSceneQPainter);
  connect(packaging_->mainScene(), &SceneWidget::sceneMouseEventGenerated, this, &QtView::sendMainSceneMouseEvent);
  connect(packaging_, &PackagingWidget::requestChangeState, changeMode_, &QAction::trigger);
  connect(packaging_, &PackagingWidget::requestOpenScene, this, &QtView::generateOpenSceneFileEvent);
  connect(packaging_, &PackagingWidget::requestSave, this, &QtView::generateSaveFileEvent);
  connect(packaging_, &PackagingWidget::requestLoad, this, &QtView::generateLoadFileEvent);
  connect(packaging_, &PackagingWidget::requestUndo, this, &QtView::generateUndoEvent);
  connect(packaging_, &PackagingWidget::requestRedo, this, &QtView::generateRedoEvent);
  connect(packaging_, &PackagingWidget::requestAutoPlace, [this]() { sendEvent(ActionEvent(Action::AutoPlace)); });
  connect(packaging_, &PackagingWidget::requestAutoPackAll, [this]() { sendEvent(ActionEvent(Action::AutoPackAll)); });
  connect(open_, &QAction::triggered, this, &QtView::generateOpenSceneFileEvent);
  connect(loadPool_, &QAction::triggered, this, &QtView::generateLoadFileEvent);
  connect(save_, &QAction::triggered, this, &QtView::generateSaveFileEvent);
  connect(saveAs_, &QAction::triggered, this, &QtView::generateSaveFileEvent);
  connect(undo_, &QAction::triggered, this, &QtView::generateUndoEvent);
  connect(redo_, &QAction::triggered, this, &QtView::generateRedoEvent);
  connect(changeMode_, &QAction::triggered, this, &QtView::generateChangeStateEvent);
}


//------------------------------------------------------------------------------
/**
  Включает или отключает действие GUI
*/
//---
void QtView::setActionEnabled(PackagingAction action, bool isEnabled)
{
  switch (action)
  {
    case PackagingAction::OpenScene:
      open_->setEnabled(isEnabled);
      packaging_->changeEnableButton(PackagingWidgetButton::OpenScene, isEnabled);
      break;
    case PackagingAction::Load:
      loadPool_->setEnabled(isEnabled);
      packaging_->changeEnableButton(PackagingWidgetButton::Load, isEnabled);
      break;
    case PackagingAction::Save:
      save_->setEnabled(isEnabled);
      packaging_->changeEnableButton(PackagingWidgetButton::Save, isEnabled);
      break;
    case PackagingAction::SaveAs:
      saveAs_->setEnabled(isEnabled);
      packaging_->changeEnableButton(PackagingWidgetButton::Save, isEnabled);
      break;
    case PackagingAction::Undo:
      undo_->setEnabled(isEnabled);
      packaging_->changeEnableButton(PackagingWidgetButton::Undo, isEnabled);
      break;
    case PackagingAction::Redo:
      redo_->setEnabled(isEnabled);
      packaging_->changeEnableButton(PackagingWidgetButton::Redo, isEnabled);
      break;
    case PackagingAction::ChangeMode:
      changeMode_->setEnabled(isEnabled);
      packaging_->changeEnableButton(PackagingWidgetButton::ChangeState, isEnabled);
      break;
  }
}


//------------------------------------------------------------------------------
/**
  Возвращает состояние доступности действия GUI
*/
//---
bool QtView::isActionEnabled(PackagingAction action) const
{
  switch (action)
  {
    case PackagingAction::OpenScene:
      return open_->isEnabled();
    case PackagingAction::Load:
      return loadPool_->isEnabled();
    case PackagingAction::Save:
      return save_->isEnabled();
    case PackagingAction::SaveAs:
      return saveAs_->isEnabled();
    case PackagingAction::Undo:
      return undo_->isEnabled();
    case PackagingAction::Redo:
      return redo_->isEnabled();
    case PackagingAction::ChangeMode:
      return changeMode_->isEnabled();
  }

  return false;
}


//------------------------------------------------------------------------------
/**
  Открывает диалог сохранения файла
*/
//---
std::string QtView::openSaveFileDialog(const std::string & title, const std::string & initPath, const std::string & filter)
{
  return QFileDialog::getSaveFileName(this, QString::fromUtf8(title.c_str()), QString::fromUtf8(initPath.c_str()),
                                      QString::fromUtf8(filter.c_str()))
    .toStdString();
}


//------------------------------------------------------------------------------
/**
  Открывает диалог загрузки файла
*/
//---
std::string QtView::openLoadFileDialog(const std::string & title, const std::string & initPath, const std::string & filter)
{
  return QFileDialog::getOpenFileName(this, QString::fromUtf8(title.c_str()), QString::fromUtf8(initPath.c_str()),
                                      QString::fromUtf8(filter.c_str()))
    .toStdString();
}


//------------------------------------------------------------------------------
/**
  Показывает пользователю неблокирующее сообщение
*/
//---
void QtView::showMessage(const std::string & title, const std::string & message, MessageType type)
{
  if (activeToast_)
  {
    activeToast_->close();
    activeToast_->deleteLater();
    activeToast_ = nullptr;
  }

  activeToast_ = new ToastNotification(QString::fromUtf8(title.c_str()), QString::fromUtf8(message.c_str()), type, this);
  connect(activeToast_, &QObject::destroyed, this,
          [this](QObject * object)
          {
            if (activeToast_ == object)
              activeToast_ = nullptr;
          });

  positionToast();
  activeToast_->show();
  activeToast_->raise();
  activeToast_->startAutoClose(TOAST_DURATION_MS);
}


//------------------------------------------------------------------------------
/**
  Запрашивает перерисовку окна
*/
//---
void QtView::requestRedraw()
{
  packaging_->mainScene()->update();
  packaging_->generateScene()->update();
}


//------------------------------------------------------------------------------
/**
  Добавляет слушателя событий
*/
//---
void QtView::addEventListener(std::shared_ptr<EventListener> listener)
{
  listeners_.push_back(std::move(listener));
}


//------------------------------------------------------------------------------
/**
  Удаляет слушателя событий
*/
//---
void QtView::removeEventListener(std::shared_ptr<EventListener> listener)
{
  std::erase(listeners_, std::move(listener));
}


//
void QtView::setZoomFactor(double factor)
{
  zoomFactor_ = factor;
}


//
double QtView::zoomFactor() const
{
  return zoomFactor_;
}


//
void QtView::statisticChangeCountAllCells(unsigned int allCells)
{
  packaging_->changeCountAllCells(allCells);
}


//
void QtView::statisticChangeCountOccupiedCells(unsigned int occupiedCells)
{
  packaging_->changeCountOccupiedCells(occupiedCells);
}


//------------------------------------------------------------------------------
/**
  Отправляет событие отрисовки основной сцены
*/
//---
void QtView::sendMainSceneQPainter(QPainter & qpainter)
{
  Painter painter(qpainter);
  const Rect2D region = QtAdapters::fromQtRect(static_cast<QWidget *>(qpainter.device())->rect());
  qpainter.scale(zoomFactor_, zoomFactor_);
  ScenePaintEvent paintEv(painter, ScenePaint::Main, region);
  sendEvent(paintEv);
}


//------------------------------------------------------------------------------
/**
  Отправляет событие отрисовки preview-сцены
*/
//---
void QtView::sendGenerateSceneQPainter(QPainter & qpainter)
{
  Painter painter(qpainter);
  const Rect2D region = QtAdapters::fromQtRect(static_cast<QWidget *>(qpainter.device())->rect());
  ScenePaintEvent paintEv(painter, ScenePaint::Generate, region);
  sendEvent(paintEv);
}


//------------------------------------------------------------------------------
/**
  Отправляет событие мыши основной сцены
*/
//---
void QtView::sendMainSceneMouseEvent(QMouseEvent * event)
{
  Point2D localPos = QtAdapters::fromQtPoint(event->localPos());
  MouseButton button = QtAdapters::fromQtMouseButton(event->buttons());

  switch (event->type())
  {
    case QEvent::MouseButtonPress:
      sendEvent(SceneMouseEvent(EventType::MousePress, localPos, button));
      break;
    case QEvent::MouseMove:
      sendEvent(SceneMouseEvent(EventType::MouseMove, localPos, button));
      break;
    case QEvent::MouseButtonRelease:
      sendEvent(SceneMouseEvent(EventType::MouseRelease, localPos, button));
      break;
    default:
      break;
  }

  event->accept();
}


//------------------------------------------------------------------------------
/**
  Генерирует событие открытия JSON-сцены
*/
//---
void QtView::generateOpenSceneFileEvent()
{
  sendEvent(OpenSceneFileEvent());
}


//------------------------------------------------------------------------------
/**
  Генерирует событие сохранения JSON-сцены
*/
//---
void QtView::generateSaveFileEvent()
{
  sendEvent(SaveFileEvent());
}


//------------------------------------------------------------------------------
/**
  Генерирует событие загрузки OBJECT-пула
*/
//---
void QtView::generateLoadFileEvent()
{
  sendEvent(LoadFileEvent());
}


//------------------------------------------------------------------------------
/**
  Генерирует событие отмены
*/
//---
void QtView::generateUndoEvent()
{
  sendEvent(UndoEvent());
}


//------------------------------------------------------------------------------
/**
  Генерирует событие повтора
*/
//---
void QtView::generateRedoEvent()
{
  sendEvent(RedoEvent());
}


//------------------------------------------------------------------------------
/**
  Генерирует событие смены режима приложения
*/
//---
void QtView::generateChangeStateEvent()
{
  sendEvent(ChangeStateEvent());
}


//------------------------------------------------------------------------------
/**
  Генерирует action events по клавиатуре
*/
//---
void QtView::keyPressEvent(QKeyEvent * event)
{
  QMainWindow::keyPressEvent(event);

  switch (event->key())
  {
    case Qt::Key_W:
      sendEvent(ActionEvent(Action::Up));
      break;
    case Qt::Key_S:
      sendEvent(ActionEvent(Action::Down));
      break;
    case Qt::Key_D:
      sendEvent(ActionEvent(Action::Right));
      break;
    case Qt::Key_A:
      sendEvent(ActionEvent(Action::Left));
      break;
    case Qt::Key_R:
      sendEvent(ActionEvent(Action::Rotate));
      break;
    case Qt::Key_Delete:
      sendEvent(ActionEvent(Action::Delete));
      break;
    default:
      break;
  }
}


//------------------------------------------------------------------------------
/**
  Обрабатывает изменение размера окна
*/
//---
void QtView::resizeEvent(QResizeEvent * event)
{
  QMainWindow::resizeEvent(event);
  positionToast();
}


//------------------------------------------------------------------------------
/**
  Отправляет событие слушателям
*/
//---
void QtView::sendEvent(const Event & event)
{
  for (auto & listener : listeners_)
    listener->onEvent(event);
}


//------------------------------------------------------------------------------
/**
  Размещает активное всплывающее сообщение в правом нижнем углу
*/
//---
void QtView::positionToast()
{
  if (!activeToast_)
    return;

  activeToast_->adjustSize();
  const int x = width() - activeToast_->width() - TOAST_MARGIN;
  const int y = height() - activeToast_->height() - TOAST_MARGIN;
  activeToast_->move(std::max(TOAST_MARGIN, x), std::max(TOAST_MARGIN, y));
}
