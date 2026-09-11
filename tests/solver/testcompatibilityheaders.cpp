#include <gridenvironment.h>
#include <gridio.h>
#include <gridlearning.h>
#include <gridsolver.h>
#include <gridtypes.h>
#include <gtest/gtest.h>
#include <polygonenvironment.h>
#include <polygonio.h>
#include <polygonlearning.h>
#include <polygonsolver.h>
#include <polygontypes.h>

namespace aipackaging::solver
{
/// Доказывает, что плоские заголовки и compatibility target сохраняют прежний публичный API.
TEST(SolverCompatibilityHeaders, CompileAndLinkLegacyApi)
{
  const GridObjectiveDefinition gridObjective;
  const ObjectiveDefinition commonObjective;
  SolverKind kind = SolverKind::InputFirstFit;

  EXPECT_EQ(gridObjective.type, commonObjective.type);
  EXPECT_EQ(toString(kind), "input-first-fit");
  EXPECT_TRUE(parseSolverKind("beam", kind));
  EXPECT_EQ(kind, SolverKind::Beam);

  PolygonProgressStage legacyStage = PolygonProgressStage::Instances;
  SearchProgressStage commonStage = legacyStage;
  PolygonExecutionControl polygonControl;
  SearchExecutionControl commonControl = polygonControl;
  GridProblem problem;
  problem.problemId = "compatibility";
  problem.sheet = {1, 1, "cell"};
  const GridSolverExecutionResult gridResult = runGridProblem(problem, SolverConfig{}, commonControl);
  EXPECT_EQ(commonStage, SearchProgressStage::Instances);
  EXPECT_FALSE(gridResult.cancelled);
}
} // namespace aipackaging::solver
