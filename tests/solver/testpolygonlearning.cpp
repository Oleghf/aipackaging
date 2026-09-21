#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

#include <aipackaging/nesting/polygon_learning.h>
#include <gtest/gtest.h>

#include "polygonfixture.h"
#include "polygonlearning_testhook.h"

namespace
{
using namespace aipackaging::solver;
using namespace aipackaging::tests;

/// Сбрасывает закрытую точку отказа даже после досрочного выхода из теста.
class LearningHookGuard
{
public:
  /// Удаляет установленную для текущего потока функцию отказа.
  ~LearningHookGuard() { internal::setPolygonLearningFailureHook({}); }
};
} // namespace

/// Проверяет динамический каталог, переход и неизменность после ошибочного индекса.
TEST(PolygonLearning, UsesDynamicActionsAndKeepsStateOnError)
{
  std::string error;
  std::unique_ptr<PolygonLearningEnvironment> environment = PolygonLearningEnvironment::Create(problem(), error);
  ASSERT_NE(environment, nullptr) << error;
  const std::size_t initialCount = environment->actions().size();
  ASSERT_GT(initialCount, 0);
  EXPECT_THROW(environment->step(initialCount), std::out_of_range);
  EXPECT_EQ(environment->actions().size(), initialCount);
  const PolygonLearningStepResult step = environment->step(0);
  EXPECT_GT(step.reward, 0.0);
  EXPECT_EQ(step.observation.occupied.size(), 128U * 128U);
  EXPECT_FALSE(environment->isComplete());
  const PolygonPlacementObservation placement = environment->placementObservation(1, 0);
  EXPECT_EQ(placement.channels.size(), 4U * 128U * 128U);
}

/// Проверяет контракт завершения и запрет шага после полного решения.
TEST(PolygonLearning, TerminatesAndRejectsFurtherSteps)
{
  std::string error;
  std::unique_ptr<PolygonLearningEnvironment> environment = PolygonLearningEnvironment::Create(problem(), error);
  ASSERT_NE(environment, nullptr) << error;
  while (!environment->isTerminal())
    environment->step(0);
  EXPECT_TRUE(environment->isComplete());
  EXPECT_THROW(environment->step(0), std::logic_error);
}

/// Проверяет формы статического и динамического наблюдений полигональной политики.
TEST(PolygonLearning, ProvidesCompactPolicyObservations)
{
  std::string error;
  std::unique_ptr<PolygonLearningEnvironment> environment = PolygonLearningEnvironment::Create(problem(), error);
  ASSERT_NE(environment, nullptr) << error;
  const PolygonStaticObservation fixed = environment->staticObservation();
  const PolygonDynamicObservation dynamic = environment->resetCompact();
  const std::size_t instances = environment->staticObservation().instanceIndices.size();
  EXPECT_EQ(fixed.partMasks.size(), instances * 4U * 32U * 32U);
  EXPECT_EQ(fixed.orientationMask.size(), instances * 4U);
  EXPECT_EQ(fixed.partFeatures.size(), instances * 7U);
  EXPECT_EQ(dynamic.occupied.size(), 128U * 128U);
  EXPECT_EQ(dynamic.pairMask.size(), instances * 4U);
  EXPECT_TRUE(std::any_of(dynamic.pairMask.begin(), dynamic.pairMask.end(), [](unsigned char value) { return value != 0; }));
}

/// Проверяет эквивалентность частей компактного и полного наблюдений и неизменность после ошибки.
TEST(PolygonLearning, CompactObservationMatchesFullObservation)
{
  std::string error;
  std::unique_ptr<PolygonLearningEnvironment> environment = PolygonLearningEnvironment::Create(problem(), error);
  ASSERT_NE(environment, nullptr) << error;
  const PolygonObservation full = environment->observation();
  const PolygonStaticObservation fixed = environment->staticObservation();
  const PolygonDynamicObservation dynamic = environment->dynamicObservation();
  EXPECT_EQ(full.occupied, dynamic.occupied);
  EXPECT_EQ(full.clearance, dynamic.clearance);
  EXPECT_EQ(full.remaining, dynamic.remaining);
  EXPECT_EQ(full.partFeatures, fixed.partFeatures);
  EXPECT_EQ(full.objective, dynamic.objective);
  const std::vector<PolygonAction> actions = environment->actions();
  EXPECT_THROW(environment->stepCompact(actions.size()), std::out_of_range);
  EXPECT_EQ(environment->actions(), actions);
  EXPECT_EQ(environment->dynamicObservation().remaining, dynamic.remaining);
}

/// Проверяет формулу потенциала v2 и сохранение прежнего вознаграждения v1.
TEST(PolygonLearning, SelectsCompatibleRewardVersion)
{
  std::string error;
  std::unique_ptr<PolygonLearningEnvironment> legacy = PolygonLearningEnvironment::Create(problem(), error);
  ASSERT_NE(legacy, nullptr) << error;
  const PolygonLearningStepResult legacyStep = legacy->step(0);
  EXPECT_EQ(legacyStep.rewardVersion, 1);
  EXPECT_DOUBLE_EQ(legacyStep.reward, 1.0 / legacy->solution().objective.totalParts);

  PolygonLearningConfig config;
  config.rewardVersion = 2;
  EXPECT_EQ(config.catalogVersion, PolygonActionCatalogVersion::Corrected);
  std::unique_ptr<PolygonLearningEnvironment> current = PolygonLearningEnvironment::Create(problem(), config, error);
  ASSERT_NE(current, nullptr) << error;
  const PolygonLearningCompactStepResult step = current->stepCompact(0);
  ASSERT_EQ(step.componentDeltas.size(), 5U);
  const double secondary = step.componentDeltas[1] + step.componentDeltas[2] / 1024.0 +
                           step.componentDeltas[3] / (1024.0 * 1024.0) + step.componentDeltas[4] / (1024.0 * 1024.0 * 1024.0);
  const double expected = (step.componentDeltas[0] + 0.5 * secondary) / current->solution().objective.totalParts;
  EXPECT_EQ(step.rewardVersion, 2);
  EXPECT_NEAR(step.reward, expected, 1e-15);
  EXPECT_DOUBLE_EQ(step.reward, step.potentialAfter - step.potentialBefore);
}

/// Проверяет отклонение неизвестной версии вознаграждения до создания эпизода.
TEST(PolygonLearning, RejectsUnknownRewardVersion)
{
  PolygonLearningConfig config;
  config.rewardVersion = 3;
  std::string error;
  EXPECT_EQ(PolygonLearningEnvironment::Create(problem(), config, error), nullptr);
  EXPECT_FALSE(error.empty());
}

/// Проверяет, что ошибка каталога не публикует частично применённое действие или сброс.
TEST(PolygonLearning, RollsBackCatalogFailure)
{
  LearningHookGuard guard;
  std::string error;
  auto environment = PolygonLearningEnvironment::Create(problem(), error);
  ASSERT_NE(environment, nullptr) << error;
  const auto initialActions = environment->actions();
  const auto initialObservation = environment->dynamicObservation();
  const auto initialPlacements = environment->solution().placements;
  internal::setPolygonLearningFailureHook(
    [](internal::PolygonLearningStage stage)
    {
      if (stage == internal::PolygonLearningStage::Catalog)
        throw std::length_error("искусственное превышение каталога");
    });
  EXPECT_THROW(environment->stepCompact(0), std::length_error);
  EXPECT_THROW(environment->reset(), std::length_error);
  EXPECT_EQ(environment->actions(), initialActions);
  EXPECT_EQ(environment->solution().placements, initialPlacements);
  internal::setPolygonLearningFailureHook({});
  EXPECT_EQ(environment->dynamicObservation().remaining, initialObservation.remaining);
}

/// Проверяет неизменность эпизода при отказе динамического или полного наблюдения.
TEST(PolygonLearning, RollsBackObservationFailure)
{
  LearningHookGuard guard;
  std::string error;
  auto environment = PolygonLearningEnvironment::Create(problem(), error);
  ASSERT_NE(environment, nullptr) << error;
  const auto initialActions = environment->actions();
  const auto initialPlacements = environment->solution().placements;
  for (const auto failedStage : {internal::PolygonLearningStage::Observation, internal::PolygonLearningStage::FullObservation})
  {
    internal::setPolygonLearningFailureHook(
      [failedStage](internal::PolygonLearningStage stage)
      {
        if (stage == failedStage)
          throw std::runtime_error("искусственный отказ наблюдения");
      });
    EXPECT_THROW(environment->step(0), std::runtime_error);
    EXPECT_THROW(environment->reset(), std::runtime_error);
    if (failedStage == internal::PolygonLearningStage::Observation)
    {
      EXPECT_THROW(environment->stepCompact(0), std::runtime_error);
      EXPECT_THROW(environment->resetCompact(), std::runtime_error);
    }
    EXPECT_EQ(environment->actions(), initialActions);
    EXPECT_EQ(environment->solution().placements, initialPlacements);
  }
}
