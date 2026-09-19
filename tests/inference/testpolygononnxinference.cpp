#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

#include <aipackaging/inference/polygon_onnx.h>
#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_io.h>
#include <gtest/gtest.h>

namespace
{
using namespace aipackaging::inference;
using namespace aipackaging::solver;

/// Возвращает путь к неизменяемому синтетическому комплекту модели.
std::filesystem::path modelPath()
{
  return AIPACKAGING_ONNX_TEST_MODEL;
}

/// Загружает полигональный тестовый пример либо завершает тест диагностикой.
PolygonProblem loadProblem()
{
  const PolygonProblemLoadResult loaded = loadPolygonProblemFromFile(AIPACKAGING_ONNX_TEST_PROBLEM);
  EXPECT_TRUE(loaded.success) << loaded.error;
  return loaded.problem;
}

/// Копирует комплект во временный каталог для проверок повреждения файлов.
std::filesystem::path copyModel()
{
  const auto destination = std::filesystem::temp_directory_path() / "aipackaging-onnx-corrupt-model";
  std::filesystem::remove_all(destination);
  std::filesystem::create_directories(destination);
  for (const auto & entry : std::filesystem::directory_iterator(modelPath()))
    std::filesystem::copy_file(entry.path(), destination / entry.path().filename());
  return destination;
}
} // namespace

/// Проверяет строгую загрузку метаданных и идентичности модели.
TEST(PolygonOnnxInference, LoadsStrictSyntheticBundle)
{
  std::string error;
  const auto policy = PolygonOnnxPolicy::Load(modelPath().string(), error);
  ASSERT_NE(policy, nullptr) << error;
  EXPECT_EQ(policy->metadata().modelId, "synthetic-polygon-policy-smoke");
  EXPECT_EQ(policy->metadata().hiddenSize, 16);
  EXPECT_EQ(policy->metadata().modelSha256, "f0da963bd89840a41a69b1229e951e0207bcc9acd4b404d7a7bb3c0f4896f850");
}

/// Проверяет стабильность жадного и случайного выбора при одинаковом начальном значении.
TEST(PolygonOnnxInference, ProducesDeterministicValidatedSolutions)
{
  std::string error;
  const auto policy = PolygonOnnxPolicy::Load(modelPath().string(), error);
  ASSERT_NE(policy, nullptr) << error;
  const PolygonProblem problem = loadProblem();
  PolygonPolicyConfig greedy;
  greedy.mode = PolygonPolicyMode::Greedy;
  const auto first = policy->run(problem, greedy);
  const auto repeated = policy->run(problem, greedy);
  EXPECT_EQ(first.solution.placements, repeated.solution.placements);
  EXPECT_TRUE(validatePolygonSolution(problem, first.solution).success);

  PolygonPolicyConfig sampled;
  sampled.mode = PolygonPolicyMode::BestOf;
  sampled.seed = 42;
  sampled.rollouts = 1;
  const auto sampledFirst = policy->run(problem, sampled);
  const auto sampledRepeated = policy->run(problem, sampled);
  EXPECT_EQ(sampledFirst.solution.placements, sampledRepeated.solution.placements);
  EXPECT_TRUE(validatePolygonSolution(problem, sampledFirst.solution).success);
  const std::vector<PolygonPlacement> expected = {{"rectangle", 0, 2000, 28000, 90}, {"rectangle", 1, 23000, 2000, 90}};
  EXPECT_EQ(sampledFirst.solution.placements, expected);
}

/// Проверяет остановку до первого действия без публикации сохраняемого ложного результата.
TEST(PolygonOnnxInference, HonorsCancellationBetweenActions)
{
  std::string error;
  const auto policy = PolygonOnnxPolicy::Load(modelPath().string(), error);
  ASSERT_NE(policy, nullptr) << error;
  PolygonPolicyControl control;
  control.cancellationRequested = []()
  {
    return true;
  };
  const auto result = policy->run(loadProblem(), {}, control);
  EXPECT_TRUE(result.cancelled);
  EXPECT_TRUE(result.solution.placements.empty());
  EXPECT_TRUE(validatePolygonSolution(loadProblem(), result.solution).success);
}

/// Проверяет отказ от ограничения времени, которое невозможно безопасно представить часами.
TEST(PolygonOnnxInference, RejectsOversizedTimeout)
{
  std::string error;
  const auto policy = PolygonOnnxPolicy::Load(modelPath().string(), error);
  ASSERT_NE(policy, nullptr) << error;
  PolygonPolicyConfig config;
  config.timeoutMs = std::numeric_limits<std::uint64_t>::max();
  EXPECT_THROW(policy->run(loadProblem(), config), std::invalid_argument);
  EXPECT_THROW(policy->runHybrid(loadProblem(), config, {}), std::invalid_argument);
}

/// Проверяет обнаружение изменённого графа до создания сеанса ONNX Runtime.
TEST(PolygonOnnxInference, RejectsChangedGraph)
{
  const auto destination = copyModel();
  {
    std::ofstream output(destination / "encoder.onnx", std::ios::binary | std::ios::app);
    output << "damage";
  }
  std::string error;
  EXPECT_EQ(PolygonOnnxPolicy::Load(destination.string(), error), nullptr);
  EXPECT_NE(error.find("Контрольная сумма"), std::string::npos);
  std::filesystem::remove_all(destination);
}

/// Проверяет отказ от скрытых дополнительных файлов внутри каталога комплекта.
TEST(PolygonOnnxInference, RejectsUnexpectedBundleFile)
{
  const auto destination = copyModel();
  {
    std::ofstream output(destination / "unexpected.txt");
    output << "unexpected";
  }
  std::string error;
  EXPECT_EQ(PolygonOnnxPolicy::Load(destination.string(), error), nullptr);
  EXPECT_NE(error.find("ровно три файла"), std::string::npos);
  std::filesystem::remove_all(destination);
}
