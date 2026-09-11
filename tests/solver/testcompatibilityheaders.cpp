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
}
} // namespace aipackaging::solver
