#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <polygonstartpage.h>

namespace
{
constexpr int PATH_ROLE = Qt::UserRole + 1;
}

/// Создаёт доступную с клавиатуры страницу основных способов начала работы.
PolygonStartPage::PolygonStartPage(QWidget * parent)
  : QWidget(parent)
  , recentList_(new QListWidget(this))
  , exampleList_(new QListWidget(this))
  , removeRecentButton_(new QPushButton(tr("Убрать из списка"), this))
  , recoveryCard_(new QWidget(this))
  , recoveryText_(new QLabel(recoveryCard_))
  , restoreRecoveryButton_(new QPushButton(tr("Восстановить"), recoveryCard_))
{
  setObjectName(QStringLiteral("polygonStartPage"));
  auto * title = new QLabel(tr("Подготовьте задачу раскроя"), this);
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 6);
  titleFont.setBold(true);
  title->setFont(titleFont);
  auto * description = new QLabel(tr("Откройте существующую полигональную задачу или воспользуйтесь готовым примером."), this);
  description->setWordWrap(true);
  auto * open = new QPushButton(tr("Открыть задачу…"), this);
  open->setObjectName(QStringLiteral("startOpenButton"));
  open->setAccessibleName(tr("Открыть существующую задачу"));
  auto * create = new QPushButton(tr("Создать задачу"), this);
  create->setObjectName(QStringLiteral("startCreateButton"));
  create->setEnabled(false);
  create->setToolTip(tr("Создание задачи появится на этапе M7"));
  auto * importDxf = new QPushButton(tr("Импортировать DXF"), this);
  importDxf->setObjectName(QStringLiteral("startImportButton"));
  importDxf->setEnabled(false);
  importDxf->setToolTip(tr("Импорт DXF появится на этапе M7"));

  recentList_->setObjectName(QStringLiteral("recentProblemList"));
  recentList_->setAccessibleName(tr("Недавние задачи"));
  exampleList_->setObjectName(QStringLiteral("exampleProblemList"));
  exampleList_->setAccessibleName(tr("Примеры задач"));
  removeRecentButton_->setObjectName(QStringLiteral("removeRecentButton"));

  recoveryCard_->setObjectName(QStringLiteral("polygonRecoveryCard"));
  recoveryCard_->setAccessibleName(tr("Найден автоматический черновик"));
  recoveryText_->setObjectName(QStringLiteral("polygonRecoveryText"));
  recoveryText_->setWordWrap(true);
  restoreRecoveryButton_->setObjectName(QStringLiteral("restoreRecoveryButton"));
  auto * deleteRecovery = new QPushButton(tr("Удалить"), recoveryCard_);
  deleteRecovery->setObjectName(QStringLiteral("deleteRecoveryButton"));
  auto * recoveryLayout = new QHBoxLayout(recoveryCard_);
  recoveryLayout->setContentsMargins(12, 8, 12, 8);
  recoveryLayout->addWidget(recoveryText_, 1);
  recoveryLayout->addWidget(restoreRecoveryButton_);
  recoveryLayout->addWidget(deleteRecovery);
  recoveryCard_->hide();

  auto * actions = new QHBoxLayout();
  actions->addWidget(open);
  actions->addWidget(create);
  actions->addWidget(importDxf);
  actions->addStretch(1);
  auto * recentActions = new QHBoxLayout();
  recentActions->addStretch(1);
  recentActions->addWidget(removeRecentButton_);
  auto * columns = new QHBoxLayout();
  auto * recentColumn = new QVBoxLayout();
  recentColumn->addWidget(new QLabel(tr("Недавние задачи"), this));
  recentColumn->addWidget(recentList_, 1);
  recentColumn->addLayout(recentActions);
  auto * exampleColumn = new QVBoxLayout();
  exampleColumn->addWidget(new QLabel(tr("Готовые примеры"), this));
  exampleColumn->addWidget(exampleList_, 1);
  columns->addLayout(recentColumn, 1);
  columns->addLayout(exampleColumn, 1);

  auto * root = new QVBoxLayout(this);
  root->setContentsMargins(48, 42, 48, 42);
  root->addStretch(1);
  root->addWidget(title);
  root->addWidget(description);
  root->addWidget(recoveryCard_);
  root->addSpacing(12);
  root->addLayout(actions);
  root->addSpacing(24);
  root->addLayout(columns, 2);
  root->addStretch(1);

  connect(open, &QPushButton::clicked, this, &PolygonStartPage::requestOpenProblem);
  connect(recentList_, &QListWidget::itemActivated, this, [this]() { openSelected(recentList_); });
  connect(exampleList_, &QListWidget::itemActivated, this, [this]() { openSelected(exampleList_); });
  connect(removeRecentButton_, &QPushButton::clicked, this,
          [this]()
          {
            if (const auto * item = recentList_->currentItem())
              emit requestRemoveRecent(item->data(PATH_ROLE).toString());
          });
  connect(restoreRecoveryButton_, &QPushButton::clicked, this, &PolygonStartPage::requestRestoreRecovery);
  connect(deleteRecovery, &QPushButton::clicked, this, &PolygonStartPage::requestDeleteRecovery);
}

/// Формирует понятное описание источника, времени и доступности найденного черновика.
void PolygonStartPage::setRecoveryCandidate(const PolygonRecoveryCandidate & recovery)
{
  recoveryCard_->setVisible(recovery.present);
  if (!recovery.present)
    return;
  QString text = tr("Автоматический черновик от %1").arg(QString::fromStdString(recovery.savedAtUtc));
  if (!recovery.sourceIdentifier.empty())
    text += tr(". Источник: %1").arg(QString::fromStdString(recovery.sourceIdentifier));
  if (recovery.sourceChanged)
    text += tr(". Исходный файл был изменён; восстановленный документ потребуется сохранить по новому пути");
  if (!recovery.error.empty())
    text = tr("Автоматический черновик повреждён: %1").arg(QString::fromStdString(recovery.error));
  recoveryText_->setText(text);
  restoreRecoveryButton_->setEnabled(recovery.restorable);
}

/// Заполняет список в сохранённом порядке и не скрывает недоступные пути.
void PolygonStartPage::setRecentFiles(const QStringList & files)
{
  recentList_->clear();
  for (const QString & path : files)
  {
    const QFileInfo info(path);
    auto * item = new QListWidgetItem(info.fileName().isEmpty() ? path : info.fileName(), recentList_);
    item->setData(PATH_ROLE, path);
    item->setToolTip(path);
    if (!info.isFile())
    {
      item->setText(tr("%1 — файл недоступен").arg(item->text()));
    }
  }
}

/// Заполняет каталог поставляемых примеров стабильными пользовательскими названиями.
void PolygonStartPage::setExamples(const QList<QPair<QString, QString>> & examples)
{
  exampleList_->clear();
  for (const auto & [title, path] : examples)
  {
    auto * item = new QListWidgetItem(title, exampleList_);
    item->setData(PATH_ROLE, path);
    item->setToolTip(path);
    if (!QFileInfo::exists(path))
      item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
  }
}

/// Проверяет доступность выбранного элемента перед публикацией пути.
void PolygonStartPage::openSelected(QListWidget * list)
{
  const auto * item = list->currentItem();
  if (!item || !item->flags().testFlag(Qt::ItemIsEnabled))
    return;
  const QString path = item->data(PATH_ROLE).toString();
  if (QFileInfo(path).isFile())
    emit requestOpenPath(path);
}
