#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <aipackaging/nesting/grid_environment.h>
#include <aipackaging/nesting/grid_io.h>
#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_io.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace
{
using nlohmann::json;
using namespace aipackaging::solver;

/// Читает текст fixture целиком, чтобы передать его существующему strict parser.
std::string readText(const std::filesystem::path & path)
{
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    return {};
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

/// Проверяет один grid_problem через тот же parser и валидатор, что использует CLI.
void checkGridProblem(const json & item, const std::filesystem::path & corpusRoot)
{
  const GridProblemLoadResult loaded = loadGridProblemFromText(readText(corpusRoot / item.at("file").get<std::string>()));
  EXPECT_EQ(item.at("expectedParsing").get<bool>(), loaded.success);

  bool domainAccepted = false;
  if (loaded.success)
    domainAccepted = validateGridProblem(loaded.problem).success;
  EXPECT_EQ(item.at("expectedDomain").get<bool>(), domainAccepted);
}

/// Проверяет grid_solution отдельно на wire-разбор и соответствие связанной задаче.
void checkGridSolution(const json & item, const std::filesystem::path & corpusRoot)
{
  const GridSolutionLoadResult solution = loadGridSolutionFromText(readText(corpusRoot / item.at("file").get<std::string>()));
  EXPECT_EQ(item.at("expectedParsing").get<bool>(), solution.success);

  bool domainAccepted = false;
  const GridProblemLoadResult problem = loadGridProblemFromText(readText(corpusRoot / item.at("problem").get<std::string>()));
  if (solution.success && problem.success)
    domainAccepted = validateGridSolution(problem.problem, solution.solution).success;
  EXPECT_EQ(item.at("expectedDomain").get<bool>(), domainAccepted);
}

/// Проверяет один polygon_problem после аппроксимации и нормализации геометрии.
void checkPolygonProblem(const json & item, const std::filesystem::path & corpusRoot)
{
  const PolygonProblemLoadResult loaded = loadPolygonProblemFromText(readText(corpusRoot / item.at("file").get<std::string>()));
  EXPECT_EQ(item.at("expectedParsing").get<bool>(), loaded.success);

  bool domainAccepted = false;
  if (loaded.success)
    domainAccepted = validatePolygonProblem(loaded.problem).success;
  EXPECT_EQ(item.at("expectedDomain").get<bool>(), domainAccepted);
}

/// Проверяет polygon_solution точным валидатором относительно связанной задачи.
void checkPolygonSolution(const json & item, const std::filesystem::path & corpusRoot)
{
  const PolygonSolutionLoadResult solution =
    loadPolygonSolutionFromText(readText(corpusRoot / item.at("file").get<std::string>()));
  EXPECT_EQ(item.at("expectedParsing").get<bool>(), solution.success);

  bool domainAccepted = false;
  const PolygonProblemLoadResult problem =
    loadPolygonProblemFromText(readText(corpusRoot / item.at("problem").get<std::string>()));
  if (solution.success && problem.success)
    domainAccepted = validatePolygonSolution(problem.problem, solution.solution).success;
  EXPECT_EQ(item.at("expectedDomain").get<bool>(), domainAccepted);
}
} // namespace

/// Проигрывает общий manifest через C++ parsers и независимые domain validators.
TEST(ContractCorpus, ClassifiesAllCasesWithCppContracts)
{
  const std::filesystem::path corpusRoot = AIPACKAGING_CONTRACT_CORPUS_DIR;
  ASSERT_TRUE(std::filesystem::is_regular_file(corpusRoot / "manifest.json"));
  const json manifest = json::parse(readText(corpusRoot / "manifest.json"));
  ASSERT_EQ("aipackaging.contract_corpus", manifest.at("format"));
  ASSERT_EQ(1, manifest.at("version"));

  for (const json & item : manifest.at("cases"))
  {
    SCOPED_TRACE(item.at("id").get<std::string>());
    ASSERT_TRUE(std::filesystem::is_regular_file(corpusRoot / item.at("file").get<std::string>()));
    ASSERT_TRUE(std::filesystem::is_regular_file(corpusRoot / item.at("schema").get<std::string>()));

    const std::string contract = item.at("contract").get<std::string>();
    if (contract == "grid_problem")
      checkGridProblem(item, corpusRoot);
    else if (contract == "grid_solution")
      checkGridSolution(item, corpusRoot);
    else if (contract == "polygon_problem")
      checkPolygonProblem(item, corpusRoot);
    else if (contract == "polygon_solution")
      checkPolygonSolution(item, corpusRoot);
    else
      FAIL() << "Unknown corpus contract: " << contract;
  }
}
