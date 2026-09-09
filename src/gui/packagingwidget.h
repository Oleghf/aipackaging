#ifndef PACKAGINGWIDGET_H__
#define PACKAGINGWIDGET_H__

#include <QWidget>

enum class PackagingWidgetButton
{
  ChangeState,
  OpenScene,
  Save,
  Load,
  Undo,
  Redo
};

class QPushButton;
class QLabel;
class SceneWidget;

////////////////////////////////////////////////////////////////////////////////
//
/// Виджет упаковки объектов
/**
*/
////////////////////////////////////////////////////////////////////////////////
class PackagingWidget : public QWidget
{
  Q_OBJECT
public:
  PackagingWidget(QWidget * parent = nullptr);

  SceneWidget * mainScene() const;
  SceneWidget * generateScene() const;

  unsigned int countAllCells() const;
  unsigned int countOccupiedCells() const;

signals:
  void requestOpenScene();
  void requestSave();
  void requestLoad();
  void requestUndo();
  void requestRedo();
  void requestAutoPlace();
  void requestAutoPackAll();
  void requestChangeState();

public slots:
  void changeEnableButton(PackagingWidgetButton btn, bool isEnable);
  void changeCountAllCells(unsigned int allCells);
  void changeCountOccupiedCells(unsigned int occupiedCells);

private:
  SceneWidget * mainScene_;
  SceneWidget * genScene_;

  QPushButton * editBut_;
  QPushButton * openSceneBut_;
  QPushButton * saveBut_;
  QPushButton * loadBut_;
  QPushButton * undoBut_;
  QPushButton * redoBut_;
  QPushButton * manualModeBut_;
  QPushButton * automaticModeBut_;
  QPushButton * autoPackAllBut_;

  QLabel * allCellsNameLabel_;
  QLabel * allCellsCountLabel_;
  QLabel * occupiedCellsNameLabel_;
  QLabel * occupiedCellsCountLabel_;
};

#endif
