#include <algorithm>
#include <QHeaderView>
#include <QLabel>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <polygondocumentpanel.h>

/// Создаёт левую панель в прежнем порядке и с прежними именами объектов Qt.
PolygonDocumentPanel::PolygonDocumentPanel(QWidget * parent)
  : QWidget(parent)
  , problemLabel_(new QLabel(tr("Задача: —"), this))
  , partTree_(new QTreeWidget(this))
  , partDetailsLabel_(new QLabel(tr("Выберите деталь в списке или на листе"), this))
{
  setMinimumWidth(240);
  partDetailsLabel_->setWordWrap(true);
  partTree_->setObjectName(QStringLiteral("polygonPartTree"));
  partTree_->setHeaderLabels({tr("Деталь"), tr("Количество")});
  partTree_->header()->setStretchLastSection(false);
  partTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  partTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

  auto * layout = new QVBoxLayout(this);
  layout->addWidget(problemLabel_);
  layout->addWidget(new QLabel(tr("Состав задачи"), this));
  layout->addWidget(partTree_, 1);
  layout->addWidget(new QLabel(tr("Выбранная деталь"), this));
  layout->addWidget(partDetailsLabel_);
  auto * legend = new QLabel(
    tr("Обозначения: цвет — тип детали; белая область — отверстие; пунктир — отступ; зелёная область — полезный остаток."), this);
  legend->setObjectName(QStringLiteral("polygonLegend"));
  legend->setWordWrap(true);
  layout->addWidget(legend);

  connect(partTree_, &QTreeWidget::itemSelectionChanged, this,
          [this]()
          {
            if (const auto * item = partTree_->currentItem())
            {
              const QString partId = item->data(0, Qt::UserRole).toString();
              showSelectedPart(partId, 0);
              emit partSelected(partId, 0);
            }
          });
}

/// Копирует снимок, формирует размеры листа и создаёт по строке на каждый тип детали.
void PolygonDocumentPanel::present(const PolygonWorkspaceSnapshot & snapshot)
{
  snapshot_ = snapshot;
  problemLabel_->setText(snapshot.problemId.empty() ? tr("Задача: —")
                                                    : tr("Задача: %1\nЛист: %2 × %3 мм\nОтступ: %4 мм; зазор: %5 мм; рез: %6 мм")
                                                        .arg(QString::fromStdString(snapshot.problemId))
                                                        .arg(snapshot.document.sheetWidth, 0, 'f', 2)
                                                        .arg(snapshot.document.sheetHeight, 0, 'f', 2)
                                                        .arg(snapshot.document.sheetMargin, 0, 'f', 2)
                                                        .arg(snapshot.document.partSpacing, 0, 'f', 2)
                                                        .arg(snapshot.document.kerf, 0, 'f', 2));
  partTree_->clear();
  for (const PolygonPartSummary & part : snapshot.document.parts)
  {
    auto * item = new QTreeWidgetItem(partTree_);
    item->setText(0, QString::fromStdString(part.id));
    item->setText(1, QString::number(part.quantity));
    item->setData(0, Qt::UserRole, QString::fromStdString(part.id));
  }
}

/// Находит тип и размещение экземпляра и переводит их свойства в миллиметровую подпись.
void PolygonDocumentPanel::showSelectedPart(const QString & partId, std::uint32_t instanceIndex)
{
  const auto found =
    std::find_if(snapshot_.document.parts.begin(), snapshot_.document.parts.end(),
                 [&partId](const PolygonPartSummary & part) { return QString::fromStdString(part.id) == partId; });
  if (found == snapshot_.document.parts.end())
    return;
  QStringList rotations;
  for (int rotation : found->allowedRotations)
    rotations.push_back(tr("%1°").arg(rotation));
  int placedRotation = -1;
  for (const PolygonPlacedPartView & placement : snapshot_.scene.placements)
  {
    if (QString::fromStdString(placement.partId) == partId && placement.instanceIndex == instanceIndex)
    {
      placedRotation = placement.rotationDegrees;
      break;
    }
  }
  partDetailsLabel_->setText(tr("%1, экземпляр %2\nКоличество: %3\nГабариты: %4 × %5 мм\nПлощадь: %6 мм²\nПовороты: %7%8")
                               .arg(partId)
                               .arg(instanceIndex)
                               .arg(found->quantity)
                               .arg(found->width, 0, 'f', 2)
                               .arg(found->height, 0, 'f', 2)
                               .arg(found->materialArea, 0, 'f', 2)
                               .arg(rotations.join(QStringLiteral(", ")))
                               .arg(placedRotation >= 0 ? tr("\nРазмещённый поворот: %1°").arg(placedRotation) : QString()));
}
