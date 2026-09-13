#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <packagingwidget.h>
#include <scenewidget.h>

namespace
{
QLabel * makeSectionTitle(const QString & text, QWidget * parent)
{
  QLabel * label = new QLabel(text, parent);
  label->setObjectName("sectionTitle");
  return label;
}

QFrame * makeCard(QWidget * parent)
{
  QFrame * frame = new QFrame(parent);
  frame->setObjectName("panelCard");
  frame->setFrameShape(QFrame::NoFrame);
  return frame;
}

void configureControlButton(QPushButton * button)
{
  button->setMinimumHeight(34);
  button->setCursor(Qt::PointingHandCursor);
}
} // namespace


//------------------------------------------------------------------------------
/**
  \brief Создаёт виджет клеточного раскроя.
  \details Размещает оболочку режима раскроя и соединяет сигналы UI.
*/
//--
PackagingWidget::PackagingWidget(QWidget * parent)
  : mainScene_(new SceneWidget(this))
  , genScene_(new SceneWidget(this))
  , editBut_(new QPushButton(tr("Редактор"), this))
  , openSceneBut_(new QPushButton(tr("Открыть сцену"), this))
  , saveBut_(new QPushButton(tr("Сохранить сцену"), this))
  , loadBut_(new QPushButton(tr("Загрузить пул"), this))
  , undoBut_(new QPushButton(tr("Отменить"), this))
  , redoBut_(new QPushButton(tr("Повторить"), this))
  , manualModeBut_(new QPushButton(tr("Ручной"), this))
  , automaticModeBut_(new QPushButton(tr("Авторазместить"), this))
  , autoPackAllBut_(new QPushButton(tr("Упаковать всё"), this))
  , allCellsNameLabel_(new QLabel(tr("Всего клеток"), this))
  , allCellsCountLabel_(new QLabel("-1", this))
  , occupiedCellsNameLabel_(new QLabel(tr("Занято клеток"), this))
  , occupiedCellsCountLabel_(new QLabel("-1", this))
{
  setObjectName("packagingShell");
  mainScene_->setObjectName("mainScene");
  genScene_->setObjectName("previewScene");

  mainScene_->setMinimumSize({800, 600});
  mainScene_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  genScene_->setFixedSize({192, 192});

  configureControlButton(editBut_);
  configureControlButton(openSceneBut_);
  configureControlButton(saveBut_);
  configureControlButton(loadBut_);
  configureControlButton(undoBut_);
  configureControlButton(redoBut_);
  configureControlButton(automaticModeBut_);
  configureControlButton(autoPackAllBut_);

  manualModeBut_->setObjectName("activeModeButton");
  automaticModeBut_->setObjectName("autoPlaceButton");
  autoPackAllBut_->setObjectName("autoPackAllButton");
  manualModeBut_->setEnabled(false);

  QFrame * previewCard = makeCard(this);
  QVBoxLayout * previewLayout = new QVBoxLayout(previewCard);
  previewLayout->setContentsMargins(14, 14, 14, 14);
  previewLayout->setSpacing(10);
  previewLayout->addWidget(makeSectionTitle(tr("Следующая фигура"), previewCard));
  previewLayout->addWidget(genScene_, 0, Qt::AlignCenter);

  QFrame * filesCard = makeCard(this);
  QVBoxLayout * filesLayout = new QVBoxLayout(filesCard);
  filesLayout->setContentsMargins(14, 14, 14, 14);
  filesLayout->setSpacing(10);
  filesLayout->addWidget(makeSectionTitle(tr("Файлы"), filesCard));
  filesLayout->addWidget(openSceneBut_);
  filesLayout->addWidget(saveBut_);
  filesLayout->addWidget(loadBut_);

  QFrame * modeCard = makeCard(this);
  QVBoxLayout * modeLayout = new QVBoxLayout(modeCard);
  modeLayout->setContentsMargins(14, 14, 14, 14);
  modeLayout->setSpacing(10);
  modeLayout->addWidget(makeSectionTitle(tr("Режим упаковки"), modeCard));
  QGridLayout * modeButtonsLayout = new QGridLayout();
  modeButtonsLayout->setHorizontalSpacing(8);
  modeButtonsLayout->setVerticalSpacing(8);
  modeButtonsLayout->addWidget(manualModeBut_, 0, 0, 1, 2);
  modeButtonsLayout->addWidget(automaticModeBut_, 1, 0);
  modeButtonsLayout->addWidget(autoPackAllBut_, 1, 1);
  modeLayout->addLayout(modeButtonsLayout);

  QFrame * controlCard = makeCard(this);
  QVBoxLayout * controlLayout = new QVBoxLayout(controlCard);
  controlLayout->setContentsMargins(14, 14, 14, 14);
  controlLayout->setSpacing(10);
  controlLayout->addWidget(makeSectionTitle(tr("Управление"), controlCard));
  QGridLayout * controlButtonsLayout = new QGridLayout();
  controlButtonsLayout->setHorizontalSpacing(8);
  controlButtonsLayout->setVerticalSpacing(8);
  controlButtonsLayout->addWidget(undoBut_, 0, 0);
  controlButtonsLayout->addWidget(redoBut_, 0, 1);
  controlButtonsLayout->addWidget(editBut_, 1, 0, 1, 2);
  controlLayout->addLayout(controlButtonsLayout);

  QFrame * statsCard = makeCard(this);
  QGridLayout * statsLayout = new QGridLayout(statsCard);
  statsLayout->setContentsMargins(14, 14, 14, 14);
  statsLayout->setHorizontalSpacing(12);
  statsLayout->setVerticalSpacing(8);
  statsLayout->addWidget(makeSectionTitle(tr("Статистика"), statsCard), 0, 0, 1, 2);
  statsLayout->addWidget(allCellsNameLabel_, 1, 0);
  statsLayout->addWidget(allCellsCountLabel_, 1, 1, Qt::AlignRight);
  statsLayout->addWidget(occupiedCellsNameLabel_, 2, 0);
  statsLayout->addWidget(occupiedCellsCountLabel_, 2, 1, Qt::AlignRight);

  QFrame * hintsCard = makeCard(this);
  QVBoxLayout * hintsLayout = new QVBoxLayout(hintsCard);
  hintsLayout->setContentsMargins(14, 14, 14, 14);
  hintsLayout->setSpacing(8);
  hintsLayout->addWidget(makeSectionTitle(tr("Подсказки"), hintsCard));
  QLabel * hintsLabel =
    new QLabel(tr("W/A/S/D - перемещение\nR - поворот\nDelete - удалить активную фигуру\nКрасный контур - невалидное размещение"),
               hintsCard);
  hintsLabel->setObjectName("hintText");
  hintsLayout->addWidget(hintsLabel);

  QVBoxLayout * sidePanelLayout = new QVBoxLayout();
  sidePanelLayout->setContentsMargins(0, 0, 0, 0);
  sidePanelLayout->setSpacing(12);
  sidePanelLayout->addWidget(previewCard);
  sidePanelLayout->addWidget(filesCard);
  sidePanelLayout->addWidget(modeCard);
  sidePanelLayout->addWidget(controlCard);
  sidePanelLayout->addWidget(statsCard);
  sidePanelLayout->addWidget(hintsCard);
  sidePanelLayout->addStretch();

  QFrame * sidePanel = new QFrame(this);
  sidePanel->setObjectName("sidePanel");
  sidePanel->setLayout(sidePanelLayout);
  sidePanel->setFixedWidth(288);

  QFrame * sceneCard = makeCard(this);
  QVBoxLayout * sceneLayout = new QVBoxLayout(sceneCard);
  sceneLayout->setContentsMargins(14, 14, 14, 14);
  sceneLayout->setSpacing(10);
  sceneLayout->addWidget(makeSectionTitle(tr("Сцена упаковки"), sceneCard));
  sceneLayout->addWidget(mainScene_, 1);

  QHBoxLayout * rootLayout = new QHBoxLayout(this);
  rootLayout->setContentsMargins(18, 18, 18, 18);
  rootLayout->setSpacing(16);
  rootLayout->addWidget(sceneCard, 1);
  rootLayout->addWidget(sidePanel, 0);

  setStyleSheet("QWidget#packagingShell {"
                "background: #f4f7fb;"
                "font-family: 'Segoe UI', 'Noto Sans', sans-serif;"
                "color: #1f2a37;"
                "}"
                "QFrame#panelCard {"
                "background: #ffffff;"
                "border: 1px solid #dde6f0;"
                "border-radius: 14px;"
                "}"
                "QFrame#sidePanel {"
                "background: transparent;"
                "border: none;"
                "}"
                "QLabel#sectionTitle {"
                "font-size: 13px;"
                "font-weight: 700;"
                "letter-spacing: 0.3px;"
                "color: #24364b;"
                "}"
                "QLabel#hintText {"
                "font-size: 12px;"
                "line-height: 150%;"
                "color: #607086;"
                "}"
                "QPushButton {"
                "background: #edf3fa;"
                "border: 1px solid #cdd9e5;"
                "border-radius: 9px;"
                "padding: 7px 11px;"
                "font-weight: 600;"
                "color: #25364a;"
                "}"
                "QPushButton:hover:enabled {"
                "background: #e1ebf6;"
                "border-color: #9fb4c9;"
                "}"
                "QPushButton:pressed:enabled {"
                "background: #d4e2f0;"
                "}"
                "QPushButton:disabled {"
                "color: #8190a3;"
                "background: #f3f6f9;"
                "border-color: #e0e7ef;"
                "}"
                "QPushButton#activeModeButton {"
                "background: #dff6ea;"
                "border-color: #87c7a3;"
                "color: #1d6b3f;"
                "}"
                "QPushButton#disabledModeButton {"
                "background: #f5f6f8;"
                "border-color: #e1e6ed;"
                "color: #9aa6b2;"
                "}"
                "QPushButton#autoPlaceButton {"
                "background: #e8f1ff;"
                "border-color: #96b8e8;"
                "color: #1f4f8f;"
                "}"
                "QPushButton#autoPackAllButton {"
                "background: #e2f7ec;"
                "border-color: #86cda7;"
                "color: #17633d;"
                "}"
                "SceneWidget#mainScene, SceneWidget#previewScene {"
                "background: #fbfdff;"
                "border: 1px solid #d8e2ee;"
                "border-radius: 12px;"
                "}");

  connect(editBut_, &QPushButton::clicked, [this]() { emit requestChangeState(); });
  connect(openSceneBut_, &QPushButton::clicked, [this]() { emit requestOpenScene(); });
  connect(saveBut_, &QPushButton::clicked, [this]() { emit requestSave(); });
  connect(loadBut_, &QPushButton::clicked, [this]() { emit requestLoad(); });
  connect(undoBut_, &QPushButton::clicked, [this]() { emit requestUndo(); });
  connect(redoBut_, &QPushButton::clicked, [this]() { emit requestRedo(); });
  connect(automaticModeBut_, &QPushButton::clicked, [this]() { emit requestAutoPlace(); });
  connect(autoPackAllBut_, &QPushButton::clicked, [this]() { emit requestAutoPackAll(); });
}


//
SceneWidget * PackagingWidget::mainScene() const
{
  return mainScene_;
}


//
SceneWidget * PackagingWidget::generateScene() const
{
  return genScene_;
}


//
unsigned int PackagingWidget::countAllCells() const
{
  return allCellsCountLabel_->text().toUInt();
}


//
unsigned int PackagingWidget::countOccupiedCells() const
{
  return occupiedCellsCountLabel_->text().toUInt();
}


//
void PackagingWidget::changeEnableButton(PackagingWidgetButton btn, bool isEnable)
{
  switch (btn)
  {
    case PackagingWidgetButton::ChangeState:
      editBut_->setEnabled(isEnable);
      break;
    case PackagingWidgetButton::OpenScene:
      openSceneBut_->setEnabled(isEnable);
      break;
    case PackagingWidgetButton::Load:
      loadBut_->setEnabled(isEnable);
      break;
    case PackagingWidgetButton::Save:
      saveBut_->setEnabled(isEnable);
      break;
    case PackagingWidgetButton::Undo:
      undoBut_->setEnabled(isEnable);
      break;
    case PackagingWidgetButton::Redo:
      redoBut_->setEnabled(isEnable);
      break;
  }
}


//
void PackagingWidget::changeCountAllCells(unsigned int allCells)
{
  allCellsCountLabel_->setText(QString::number(allCells));
}


//
void PackagingWidget::changeCountOccupiedCells(unsigned int occupiedCells)
{
  occupiedCellsCountLabel_->setText(QString::number(occupiedCells));
}
