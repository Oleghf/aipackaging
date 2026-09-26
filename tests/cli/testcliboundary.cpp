#include <ostream>
#include <sstream>
#include <stdexcept>

#include <cli_boundary.h>
#include <gtest/gtest.h>

namespace
{
/// Возвращает переданный код без исключения.
int successfulOperation(void * context)
{
  return *static_cast<int *>(context);
}

/// Имитирует стандартное исключение обработчика CLI.
int standardFailure(void *)
{
  throw std::runtime_error("искусственный отказ");
}

/// Имитирует исключение, не производное от `std::exception`.
int unknownFailure(void *)
{
  throw 42;
}
} // namespace

/// Проверяет сохранение успешного кода и отсутствие диагностики.
TEST(CliBoundary, ReturnsSuccessfulOperationCode)
{
  int expected = 7;
  std::ostringstream diagnostics;
  EXPECT_EQ(aipackaging::cli::runCliGuarded(successfulOperation, &expected, diagnostics), expected);
  EXPECT_TRUE(diagnostics.str().empty());
}

/// Проверяет прежний текст диагностики стандартного исключения.
TEST(CliBoundary, ReportsStandardException)
{
  std::ostringstream diagnostics;
  EXPECT_EQ(aipackaging::cli::runCliGuarded(standardFailure, nullptr, diagnostics), 1);
  EXPECT_EQ(diagnostics.str(), "Внутренняя ошибка: искусственный отказ\n");
}

/// Проверяет диагностируемый код 1 для исключения неизвестного типа.
TEST(CliBoundary, ReportsUnknownException)
{
  std::ostringstream diagnostics;
  EXPECT_EQ(aipackaging::cli::runCliGuarded(unknownFailure, nullptr, diagnostics), 1);
  EXPECT_EQ(diagnostics.str(), "Неизвестная внутренняя ошибка\n");
}
