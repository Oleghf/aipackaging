#include <algorithm>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <aipackaging/editor/polygon_editor_interaction.h>
#include <polygonproblemspanel.h>

using namespace aipackaging::editor;

/// Собирает список и оставляет геометрию документа только в неизменяемом снимке.
PolygonProblemsPanel::PolygonProblemsPanel(QWidget * parent)
  : QWidget(parent)
  , list_(new QListWidget(this))
  , fixButton_(new QPushButton(tr("Исправить…"), this))
{
  setObjectName(QStringLiteral("polygonProblemsPanel"));
  list_->setObjectName(QStringLiteral("polygonProblemsList"));
  list_->setAccessibleName(tr("Проблемы полигонального документа"));
  fixButton_->setObjectName(QStringLiteral("polygonProblemFixButton"));
  fixButton_->setEnabled(false);
  auto * layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(tr("Ошибки и предупреждения текущей редакции"), this));
  layout->addWidget(list_, 1);
  layout->addWidget(fixButton_);
  connect(list_, &QListWidget::currentRowChanged, this, [this]() { presentSelection(); });
  connect(fixButton_, &QPushButton::clicked, this, &PolygonProblemsPanel::applySelectedFix);
}

/// Перестраивает строки в стабильном порядке локальной и точной проверок.
void PolygonProblemsPanel::present(const PolygonWorkspaceSnapshot & snapshot)
{
  const int selected = list_->currentRow();
  snapshot_ = snapshot;
  updating_ = true;
  list_->clear();
  for (const DocumentDiagnostic & diagnostic : snapshot.documentDiagnostics)
  {
    const QString prefix = diagnostic.severity == DiagnosticSeverity::Error ? tr("Ошибка: ") : tr("Предупреждение: ");
    list_->addItem(prefix + QString::fromStdString(diagnostic.message));
  }
  if (selected >= 0 && selected < list_->count())
    list_->setCurrentRow(selected);
  updating_ = false;
  presentSelection();
}

/// Выбирает первую диагностику, связанную хотя бы с одной указанной сущностью.
void PolygonProblemsPanel::selectEntities(const std::vector<std::uint64_t> & entities)
{
  if (updating_ || entities.empty())
    return;
  for (std::size_t index = 0; index < snapshot_.documentDiagnostics.size(); ++index)
  {
    const DocumentDiagnostic & diagnostic = snapshot_.documentDiagnostics[index];
    const auto contains = [&](EntityId id)
    {
      return std::find(entities.begin(), entities.end(), id.value) != entities.end();
    };
    if (contains(diagnostic.entity) ||
        std::any_of(diagnostic.relatedEntities.begin(), diagnostic.relatedEntities.end(), contains))
    {
      updating_ = true;
      list_->setCurrentRow(static_cast<int>(index));
      updating_ = false;
      return;
    }
  }
}

/// Собирает основной и связанные идентификаторы без изменения документа.
void PolygonProblemsPanel::presentSelection()
{
  const int row = list_->currentRow();
  if (row < 0 || static_cast<std::size_t>(row) >= snapshot_.documentDiagnostics.size())
  {
    fixButton_->setEnabled(false);
    return;
  }
  const DocumentDiagnostic & diagnostic = snapshot_.documentDiagnostics[static_cast<std::size_t>(row)];
  std::vector<std::uint64_t> entities;
  if (diagnostic.entity)
    entities.push_back(diagnostic.entity.value);
  for (EntityId related : diagnostic.relatedEntities)
    if (related && std::find(entities.begin(), entities.end(), related.value) == entities.end())
      entities.push_back(related.value);
  fixButton_->setEnabled(snapshot_.canEdit && snapshot_.editableDocument &&
                         suggestedDiagnosticFix(*snapshot_.editableDocument, diagnostic).has_value());
  if (!updating_)
    emit entitiesSelected(entities);
}

/// Не применяет даже однозначное исправление без отдельного пользовательского подтверждения.
void PolygonProblemsPanel::applySelectedFix()
{
  const int row = list_->currentRow();
  if (!snapshot_.canEdit || !snapshot_.editableDocument || row < 0 ||
      static_cast<std::size_t>(row) >= snapshot_.documentDiagnostics.size())
    return;
  const auto fix =
    suggestedDiagnosticFix(*snapshot_.editableDocument, snapshot_.documentDiagnostics[static_cast<std::size_t>(row)]);
  if (!fix)
    return;
  if (QMessageBox::question(
        this, tr("Исправление геометрии"),
        tr("Применить изменение «%1»? Оно попадёт в историю и может быть отменено.").arg(QString::fromStdString(fix->label))) ==
      QMessageBox::Yes)
    emit fixRequested(*fix);
}
