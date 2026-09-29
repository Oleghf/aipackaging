#include <algorithm>
#include <string>

#include <aipackaging/import/dxf_import.h>
#include <gtest/gtest.h>

namespace
{
/// Оборачивает переданные записи в минимальный корректный ASCII DXF с миллиметрами.
std::string dxf(std::string_view entities, int units = 4)
{
  return "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1027\n9\n$INSUNITS\n70\n" + std::to_string(units) +
         "\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n" + std::string(entities) + "0\nENDSEC\n0\nEOF\n";
}

/// Возвращает квадрат из четырёх отдельных отрезков для проверки сборки цепочки.
std::string squareEntities()
{
  return "0\nLINE\n8\nCUT\n10\n0\n20\n0\n11\n20\n21\n0\n"
         "0\nLINE\n8\nCUT\n10\n20\n20\n0\n11\n20\n21\n10\n"
         "0\nLINE\n8\nCUT\n10\n20\n20\n10\n11\n0\n21\n10\n"
         "0\nLINE\n8\nCUT\n10\n0\n20\n10\n11\n0\n21\n0\n";
}
} // namespace

/// Проверяет строгий разбор единиц, слоя и замкнутой цепочки.
TEST(DxfImport, InspectsDeterministicSquare)
{
  const auto first = aipackaging::dxf::inspectAsciiDxf(dxf(squareEntities()));
  const auto second = aipackaging::dxf::inspectAsciiDxf(dxf(squareEntities()));
  ASSERT_TRUE(first.parsed);
  EXPECT_EQ(first.detectedUnit, aipackaging::dxf::LengthUnit::Millimeter);
  ASSERT_EQ(first.layers.size(), 1U);
  EXPECT_EQ(first.layers[0].name, "CUT");
  ASSERT_EQ(first.paths.size(), 1U);
  EXPECT_TRUE(first.paths[0].closed);
  EXPECT_EQ(first.paths[0].segmentCount, 4U);
  EXPECT_EQ(first.paths[0].minX, second.paths[0].minX);
  EXPECT_EQ(first.paths[0].id, second.paths[0].id);
}

/// Проверяет явный выбор единиц для безразмерного DXF и перевод дюймов.
TEST(DxfImport, RequiresUnknownUnitAndConvertsInches)
{
  auto unknown = aipackaging::dxf::inspectAsciiDxf(dxf(squareEntities(), 0));
  EXPECT_TRUE(unknown.unitSelectionRequired);
  EXPECT_TRUE(unknown.parsed);
  EXPECT_TRUE(unknown.paths.empty());
  auto inches = aipackaging::dxf::inspectAsciiDxf(dxf(squareEntities(), 1));
  ASSERT_TRUE(inches.parsed);
  ASSERT_EQ(inches.paths.size(), 1U);
  EXPECT_DOUBLE_EQ(inches.paths[0].maxX, 508.0);
  EXPECT_DOUBLE_EQ(inches.paths[0].maxY, 254.0);
}

/// Проверяет предложение, но не автоматическое применение соединения почти совпавших концов.
TEST(DxfImport, ProposesApproximateJoin)
{
  const std::string entities = "0\nLINE\n8\nCUT\n10\n0\n20\n0\n11\n10\n21\n0\n"
                               "0\nLINE\n8\nCUT\n10\n10.005\n20\n0\n11\n10\n21\n10\n";
  const auto inspection = aipackaging::dxf::inspectAsciiDxf(dxf(entities), std::nullopt, 0.01);
  ASSERT_TRUE(inspection.parsed);
  ASSERT_EQ(inspection.paths.size(), 2U);
  ASSERT_EQ(inspection.joinProposals.size(), 1U);
  EXPECT_NEAR(inspection.joinProposals[0].distanceMm, 0.005, 1e-9);
}

/// Проверяет направление `bulge` и устойчивое представление окружности двумя дугами.
TEST(DxfImport, ReadsBulgeAndCircle)
{
  const std::string entities = "0\nLWPOLYLINE\n8\nARC\n90\n2\n70\n0\n10\n0\n20\n0\n42\n1\n10\n10\n20\n0\n"
                               "0\nCIRCLE\n8\nROUND\n10\n20\n20\n20\n40\n5\n";
  const auto inspection = aipackaging::dxf::inspectAsciiDxf(dxf(entities));
  ASSERT_TRUE(inspection.parsed);
  ASSERT_EQ(inspection.paths.size(), 2U);
  EXPECT_TRUE(std::any_of(inspection.paths.begin(), inspection.paths.end(),
                          [](const auto & path) { return path.closed && path.segmentCount == 2; }));
}

/// Проверяет перевод простого зажатого кубического сплайна в один сегмент Bézier.
TEST(DxfImport, ReadsClampedCubicSpline)
{
  const std::string spline = "0\nSPLINE\n8\nCURVE\n70\n0\n71\n3\n72\n8\n73\n4\n"
                             "40\n0\n40\n0\n40\n0\n40\n0\n40\n1\n40\n1\n40\n1\n40\n1\n"
                             "10\n0\n20\n0\n30\n0\n10\n5\n20\n10\n30\n0\n"
                             "10\n15\n20\n10\n30\n0\n10\n20\n20\n0\n30\n0\n";
  const auto inspection = aipackaging::dxf::inspectAsciiDxf(dxf(spline));
  ASSERT_TRUE(inspection.parsed);
  ASSERT_EQ(inspection.paths.size(), 1U);
  EXPECT_EQ(inspection.paths[0].segmentCount, 1U);
  EXPECT_FALSE(inspection.paths[0].closed);
}

/// Проверяет отказ от двоичного и оборванного представлений.
TEST(DxfImport, RejectsBinaryAndTruncatedInput)
{
  EXPECT_FALSE(aipackaging::dxf::inspectAsciiDxf("AutoCAD Binary DXF\r\n").parsed);
  EXPECT_FALSE(aipackaging::dxf::inspectAsciiDxf("0\nSECTION\n2\n").parsed);
}

/// Проверяет блокировку неподдерживаемой сущности выбранного слоя без подтверждения.
TEST(DxfImport, RequiresUnsupportedEntityConfirmation)
{
  const std::string source = dxf(squareEntities() + "0\nTEXT\n8\nCUT\n1\nlabel\n");
  aipackaging::dxf::ImportOptions options;
  options.problemId = "imported";
  options.sheetWidth = 30.0;
  options.sheetHeight = 20.0;
  auto rejected = aipackaging::dxf::importAsciiDxf(source, options);
  EXPECT_FALSE(rejected.built);
  options.ignoreUnsupportedOnSelectedLayers = true;
  auto accepted = aipackaging::dxf::importAsciiDxf(source, options);
  EXPECT_TRUE(accepted.built);
  ASSERT_EQ(accepted.document.parts.size(), 1U);
  ASSERT_TRUE(accepted.document.parts[0].outer.has_value());
  EXPECT_DOUBLE_EQ(accepted.document.parts[0].outer->vertices.front().x, 0.0);
  EXPECT_DOUBLE_EQ(accepted.document.parts[0].outer->vertices.front().y, 0.0);
}
