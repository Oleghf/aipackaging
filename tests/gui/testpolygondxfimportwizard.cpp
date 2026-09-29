#include <memory>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>

#include <gtest/gtest.h>
#include <polygondxfimportwizard.h>

namespace
{
/// Возвращает общий экземпляр приложения Qt для проверок мастера.
QApplication * application()
{
  if (auto * existing = qobject_cast<QApplication *>(QCoreApplication::instance()))
    return existing;
  static int argumentCount = 1;
  static char name[] = "aipackaging-dxf-tests";
  static char * arguments[] = {name, nullptr};
  static auto created = std::make_unique<QApplication>(argumentCount, arguments);
  return created.get();
}

/// Создаёт готовый снимок анализа с внешним кольцом и отверстием.
PolygonImportSnapshot readySnapshot()
{
  PolygonImportSnapshot snapshot;
  snapshot.state = PolygonImportState::Ready;
  snapshot.canBuild = true;
  snapshot.inspection.success = true;
  snapshot.inspection.detectedUnit = PolygonImportUnit::Millimeter;
  snapshot.inspection.layers.push_back({"CUT", 2, 0, true});
  snapshot.inspection.paths.push_back({1, "CUT", true, 0.0, 0.0, 20.0, 10.0, 4, 0});
  snapshot.inspection.paths.push_back({2, "CUT", true, 5.0, 3.0, 8.0, 6.0, 2, 1});
  snapshot.statusText = "DXF проанализирован";
  return snapshot;
}
} // namespace

/// Проверяет пять страниц, доступные имена и начальное заполнение анализа.
TEST(PolygonDxfImportWizard, PresentsFiveImportSteps)
{
  application();
  PolygonDxfImportWizard wizard;
  PolygonImportInspectionRequest request;
  PolygonImportActions actions;
  actions.inspect = [&request](const PolygonImportInspectionRequest & value)
  {
    request = value;
  };
  wizard.startImport(QStringLiteral("sample.dxf"), std::move(actions));
  EXPECT_EQ(request.filePath, "sample.dxf");
  wizard.present(readySnapshot());
  EXPECT_EQ(wizard.pageIds().size(), 5);
  ASSERT_NE(wizard.findChild<QComboBox *>("dxfUnitCombo"), nullptr);
  ASSERT_NE(wizard.findChild<QListWidget *>("dxfLayersList"), nullptr);
  ASSERT_NE(wizard.findChild<QTableWidget *>("dxfAssignmentsTable"), nullptr);
  EXPECT_EQ(wizard.findChild<QListWidget *>("dxfLayersList")->count(), 1);
  EXPECT_EQ(wizard.findChild<QTableWidget *>("dxfAssignmentsTable")->rowCount(), 2);
}

/// Проверяет формирование задачи, отверстия и обязательное подтверждение параметров листа.
TEST(PolygonDxfImportWizard, BuildsNeutralConfiguration)
{
  application();
  PolygonDxfImportWizard wizard;
  PolygonImportConfiguration captured;
  PolygonImportActions actions;
  actions.inspect = [](const PolygonImportInspectionRequest &) {};
  actions.build = [&captured](const PolygonImportConfiguration & value)
  {
    captured = value;
  };
  wizard.startImport(QStringLiteral("sample.dxf"), std::move(actions));
  wizard.present(readySnapshot());
  wizard.setCurrentId(3);
  wizard.setCurrentId(4);
  ASSERT_NE(wizard.button(QWizard::FinishButton), nullptr);
  wizard.button(QWizard::FinishButton)->click();
  ASSERT_EQ(captured.parts.size(), 1U);
  EXPECT_EQ(captured.parts[0].outerPath, 1U);
  ASSERT_EQ(captured.parts[0].holes.size(), 1U);
  EXPECT_EQ(captured.parts[0].holes[0], 2U);
  EXPECT_EQ(captured.problemId, "sample");
  EXPECT_DOUBLE_EQ(captured.sheetWidth, 20.0);
  EXPECT_DOUBLE_EQ(captured.sheetHeight, 10.0);
}

/// Проверяет переключение итоговой команды между строгой задачей и черновиком.
TEST(PolygonDxfImportWizard, AcceptsInvalidResultOnlyAsDraft)
{
  application();
  PolygonDxfImportWizard wizard;
  bool acceptedDraft = false;
  PolygonImportActions actions;
  actions.inspect = [](const PolygonImportInspectionRequest &) {};
  actions.accept = [&acceptedDraft](bool draft)
  {
    acceptedDraft = draft;
  };
  wizard.startImport(QStringLiteral("sample.dxf"), std::move(actions));
  PolygonImportSnapshot snapshot = readySnapshot();
  snapshot.state = PolygonImportState::Completed;
  snapshot.canAcceptDraft = true;
  snapshot.resultValid = false;
  wizard.present(snapshot);
  wizard.setCurrentId(4);
  wizard.button(QWizard::FinishButton)->click();
  EXPECT_TRUE(acceptedDraft);
}
