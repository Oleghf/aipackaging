#include <memory>
#include <stdexcept>
#include <string>

#include <aipackaging/nesting/polygon_learning.h>
#include <gtest/gtest.h>

#include "polygonfixture.h"

namespace
{
using namespace aipackaging::solver;
using namespace aipackaging::tests;
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

/// Проверяет terminal-контракт и запрет шага после полного решения.
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
