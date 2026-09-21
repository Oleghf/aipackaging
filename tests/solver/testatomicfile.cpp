#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include <aipackaging/nesting/grid_io.h>
#include <aipackaging/nesting/grid_solver.h>
#include <aipackaging/nesting/polygon_io.h>
#include <aipackaging/nesting/polygon_solver.h>
#include <gtest/gtest.h>

#include "atomicfile.h"
#include "polygonfixture.h"

namespace
{
using namespace aipackaging::solver;

/// Создаёт уникальный временный каталог только для данного теста.
std::filesystem::path testDirectory()
{
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto path = std::filesystem::temp_directory_path() / ("aipackaging-atomic-" + std::to_string(nonce));
  std::filesystem::create_directory(path);
  return path;
}

/// Читает весь файл побайтно для проверки сохранности результата.
std::string readFile(const std::filesystem::path & path)
{
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

/// Удаляет только временный каталог, созданный данным тестом.
class TemporaryDirectory
{
public:
  /// Создаёт изолированный каталог для проверки файлового ввода-вывода.
  TemporaryDirectory()
    : path(testDirectory())
  {
  }
  /// Убирает созданный каталог и его тестовые файлы.
  ~TemporaryDirectory() noexcept
  {
    try
    {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
    catch (...)
    {
      // Ошибка удаления тестового каталога не должна завершать процесс при размотке стека.
      std::fputs("Не удалось удалить временный каталог теста\n", stderr);
    }
  }
  std::filesystem::path path;
};
} // namespace

/// Проверяет сохранение прежнего файла при отказе во время записи и замены.
TEST(AtomicSolutionFile, PreservesExistingDestinationOnFailure)
{
  TemporaryDirectory directory;
  const auto target = directory.path / "solution.json";
  for (const auto stage : {internal::AtomicWriteStage::AfterPartialWrite, internal::AtomicWriteStage::BeforeReplace})
  {
    {
      std::ofstream output(target, std::ios::binary);
      output << "старый результат";
    }
    std::string error;
    EXPECT_FALSE(internal::writeFileAtomically(target, "новый результат", error,
                                               [stage](internal::AtomicWriteStage current)
                                               {
                                                 if (current == stage)
                                                   throw std::runtime_error("искусственный отказ");
                                               }));
    EXPECT_EQ(readFile(target), "старый результат");
    EXPECT_NE(error.find("искусственный отказ"), std::string::npos);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory.path), std::filesystem::directory_iterator()), 1);
  }
}

/// Проверяет, что неудачная запись не создаёт назначения и не оставляет временный файл.
TEST(AtomicSolutionFile, RemovesTemporaryFileForNewDestination)
{
  TemporaryDirectory directory;
  const auto target = directory.path / "solution.json";
  std::string error;
  EXPECT_FALSE(internal::writeFileAtomically(target, "новый результат", error,
                                             [](internal::AtomicWriteStage stage)
                                             {
                                               if (stage == internal::AtomicWriteStage::BeforeReplace)
                                                 throw std::runtime_error("искусственный отказ");
                                             }));
  EXPECT_FALSE(std::filesystem::exists(target));
  EXPECT_TRUE(std::filesystem::is_empty(directory.path));
}

/// Проверяет неизменные JSON-байты для обоих публичных форматов решения.
TEST(AtomicSolutionFile, PublicGridAndPolygonSavesUseCanonicalBytes)
{
  TemporaryDirectory directory;
  GridProblem grid;
  grid.problemId = "atomic-grid";
  grid.sheet = {2, 2, "cell"};
  grid.parts = {{"cell", 1, {{0, 0}}, {0}}};
  const GridSolution gridSolution = solveGridProblem(grid);
  const PolygonSolution polygonSolution = solvePolygonProblem(aipackaging::tests::problem());
  std::string error;
  const auto gridPath = directory.path / "grid.json";
  const auto polygonPath = directory.path / "polygon.json";
  ASSERT_TRUE(saveGridSolutionToFile(gridPath.string(), gridSolution, error)) << error;
  ASSERT_TRUE(savePolygonSolutionToFile(polygonPath.string(), polygonSolution, error)) << error;
  EXPECT_EQ(readFile(gridPath), saveGridSolutionToText(gridSolution));
  EXPECT_EQ(readFile(polygonPath), savePolygonSolutionToText(polygonSolution));
  ASSERT_TRUE(savePolygonSolutionToFile(polygonPath.string(), polygonSolution, error)) << error;
  EXPECT_EQ(readFile(polygonPath), savePolygonSolutionToText(polygonSolution));
}

/// Отклоняет каталог вместо файла назначения без изменения его содержимого.
TEST(AtomicSolutionFile, RejectsDirectoryDestination)
{
  TemporaryDirectory directory;
  std::string error;
  EXPECT_FALSE(internal::writeFileAtomically(directory.path, "данные", error));
  EXPECT_TRUE(std::filesystem::is_empty(directory.path));
}

/// Не заменяет символьную ссылку и не изменяет файл, на который она указывает.
TEST(AtomicSolutionFile, RejectsSymbolicLinkDestination)
{
  TemporaryDirectory directory;
  const auto original = directory.path / "original.json";
  const auto link = directory.path / "linked.json";
  {
    std::ofstream output(original, std::ios::binary);
    output << "исходные данные";
  }
  std::error_code linkError;
  std::filesystem::create_symlink(original, link, linkError);
  if (linkError)
    GTEST_SKIP() << "создание символьной ссылки недоступно: " << linkError.message();
  std::string error;
  EXPECT_FALSE(internal::writeFileAtomically(link, "новые данные", error));
  EXPECT_EQ(readFile(original), "исходные данные");
  EXPECT_TRUE(std::filesystem::is_symlink(link));
}
