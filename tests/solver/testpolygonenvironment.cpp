#include <memory>
#include <numbers>
#include <string>

#include <aipackaging/nesting/polygon_environment.h>
#include <gtest/gtest.h>

#include "polygonfixture.h"

namespace
{
using namespace aipackaging::solver;
using namespace aipackaging::tests;
} // namespace

/// Проверяет микронную нормализацию и дедупликацию симметричных поворотов.
TEST(PolygonEnvironment, NormalizesAndDeduplicatesRotations)
{
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(problem(), error);
  ASSERT_NE(environment, nullptr) << error;
  EXPECT_EQ(environment->sheetWidth(), 100000);
  EXPECT_EQ(environment->partSpacing(), 1000);
  EXPECT_EQ(environment->orientations(0).size(), 2);
}

/// Проверяет, что отверстие сохраняет площадь, но не разрешает вложение детали.
TEST(PolygonEnvironment, PreservesHoleAndRejectsNestedPlacement)
{
  PolygonProblem value = problem();
  value.parts[0].quantity = 1;
  value.parts[0].holes.push_back(rectangle(10.0, 10.0, 10.0, 5.0));
  PolygonPart inner;
  inner.id = "inner";
  inner.outer = rectangle(5.0, 5.0);
  inner.allowedRotations = {0};
  value.parts.push_back(inner);
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(value, error);
  ASSERT_NE(environment, nullptr) << error;
  PolygonState state = environment->initialState();
  ASSERT_TRUE(environment->apply(state, {"rectangle", 0, 2000, 2000, 0}));
  EXPECT_FALSE(environment->canApply(state, {"inner", 0, 14000, 9000, 0}));
  EXPECT_EQ(environment->orientations(0).front().materialArea, 500000000ULL);
}

/// Проверяет разрешённое касание при нулевом зазоре и запрет при положительном.
TEST(PolygonEnvironment, EnforcesSpacingButAllowsZeroTouch)
{
  PolygonProblem value = problem();
  value.manufacturing.sheetMargin = 0.0;
  value.manufacturing.partSpacing = 0.0;
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(value, error);
  ASSERT_NE(environment, nullptr) << error;
  PolygonState state = environment->initialState();
  ASSERT_TRUE(environment->apply(state, {"rectangle", 0, 0, 0, 0}));
  EXPECT_TRUE(environment->canApply(state, {"rectangle", 1, 30000, 0, 0}));

  value.manufacturing.partSpacing = 1.0;
  environment = PolygonEnvironment::Create(value, error);
  state = environment->initialState();
  ASSERT_TRUE(environment->apply(state, {"rectangle", 0, 0, 0, 0}));
  EXPECT_FALSE(environment->canApply(state, {"rectangle", 1, 30000, 0, 0}));
  EXPECT_TRUE(environment->canApply(state, {"rectangle", 1, 31000, 0, 0}));
}

/// Проверяет контролируемую аппроксимацию круговых дуг и кубической Bézier.
TEST(PolygonEnvironment, ApproximatesArcAndBezierPaths)
{
  PolygonProblem value = problem();
  value.parts.clear();
  PolygonPart circle;
  circle.id = "circle";
  circle.outer.start = {0.0, 10.0};
  circle.outer.segments = {{PolygonSegmentKind::Arc, {20.0, 10.0}, {10.0, 10.0}, {}, {}, false},
                           {PolygonSegmentKind::Arc, {0.0, 10.0}, {10.0, 10.0}, {}, {}, false}};
  circle.allowedRotations = {0, 90};
  value.parts.push_back(circle);
  PolygonPart curved;
  curved.id = "bezier";
  curved.outer.start = {0.0, 0.0};
  curved.outer.segments = {{PolygonSegmentKind::Line, {20.0, 0.0}},
                           {PolygonSegmentKind::Line, {20.0, 10.0}},
                           {PolygonSegmentKind::CubicBezier, {0.0, 10.0}, {}, {15.0, 18.0}, {5.0, 18.0}},
                           {PolygonSegmentKind::Line, {0.0, 0.0}}};
  curved.allowedRotations = {0};
  value.parts.push_back(curved);
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(value, error);
  ASSERT_NE(environment, nullptr) << error;
  EXPECT_GT(environment->orientations(0).front().outer.size(), 16);
  EXPECT_GT(environment->orientations(1).front().outer.size(), 4);
  EXPECT_NEAR(static_cast<double>(environment->orientations(0).front().materialArea), 100.0 * std::numbers::pi * 1000000.0,
              2100000.0);
}

/// Проверяет отказ от самопересечения и касающегося внешней границы отверстия.
TEST(PolygonEnvironment, RejectsInvalidTopology)
{
  PolygonProblem value = problem();
  value.parts[0].quantity = 1;
  value.parts[0].outer.start = {0.0, 0.0};
  value.parts[0].outer.segments = {{PolygonSegmentKind::Line, {20.0, 20.0}},
                                   {PolygonSegmentKind::Line, {0.0, 20.0}},
                                   {PolygonSegmentKind::Line, {20.0, 0.0}},
                                   {PolygonSegmentKind::Line, {0.0, 0.0}}};
  std::string error;
  EXPECT_EQ(PolygonEnvironment::Create(value, error), nullptr);

  value = problem();
  value.parts[0].quantity = 1;
  value.parts[0].holes.push_back(rectangle(10.0, 5.0, 0.0, 5.0));
  EXPECT_EQ(PolygonEnvironment::Create(value, error), nullptr);
}

/// Проверяет детерминированность NFP-кандидатов и точные primary-компоненты.
TEST(PolygonEnvironment, GeneratesStableNfpCandidatesAndObjective)
{
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(problem(), error);
  ASSERT_NE(environment, nullptr) << error;
  PolygonState state = environment->initialState();
  const auto first = environment->enumerateCandidates(state, 0);
  EXPECT_EQ(first, environment->enumerateCandidates(state, 0));
  ASSERT_TRUE(environment->apply(state, first.front()));
  const auto second = environment->enumerateCandidates(state, 1);
  EXPECT_EQ(second, environment->enumerateCandidates(state, 1));
  ASSERT_FALSE(second.empty());
  ASSERT_TRUE(environment->apply(state, second.front()));
  const PolygonObjectiveComponents objective = environment->evaluate(state);
  EXPECT_EQ(objective.usedLength, 30000);
  EXPECT_EQ(objective.primaryRemnantWidth, 66000);
  EXPECT_EQ(objective.placedParts, 2);
}

/// Проверяет сохранение точечного inner-fit кандидата при точном размере листа.
TEST(PolygonEnvironment, GeneratesCandidateForExactSheetFit)
{
  PolygonProblem value = problem();
  value.sheet = {30.0, 20.0, "mm"};
  value.manufacturing.sheetMargin = 0.0;
  value.manufacturing.partSpacing = 0.0;
  value.parts[0].quantity = 1;
  value.parts[0].allowedRotations = {0};
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(value, error);
  ASSERT_NE(environment, nullptr) << error;
  const auto candidates = environment->enumerateCandidates(environment->initialState(), 0);
  ASSERT_EQ(candidates.size(), 1);
  EXPECT_EQ(candidates.front(), (PolygonAction{"rectangle", 0, 0, 0, 0}));
}

/// Проверяет ограничения размера листа и числа обязательных экземпляров.
TEST(PolygonEnvironment, RejectsContractComplexityLimits)
{
  std::string error;
  PolygonProblem value = problem();
  value.sheet.width = 10000.001;
  EXPECT_EQ(PolygonEnvironment::Create(value, error), nullptr);
  value = problem();
  value.parts[0].quantity = 101;
  EXPECT_EQ(PolygonEnvironment::Create(value, error), nullptr);
}
