#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "strictjson.h"

namespace
{
using aipackaging::solver::internal::Json;
using aipackaging::solver::internal::StrictJsonError;
using aipackaging::solver::internal::StrictJsonResult;
using namespace aipackaging::solver::internal;
} // namespace

/// Проверяет структурированную классификацию синтаксической ошибки и неизвестного поля.
TEST(StrictJson, ClassifiesSyntaxAndUnknownFields)
{
  Json value;
  const StrictJsonResult syntax = parseDocument("{", value);
  EXPECT_EQ(syntax.error, StrictJsonError::InvalidSyntax);
  EXPECT_FALSE(syntax.detail.empty());

  value = {{"known", 1}, {"unknown", 2}};
  const StrictJsonResult fields = onlyKeys(value, {"known"});
  EXPECT_EQ(fields.error, StrictJsonError::UnknownField);
  EXPECT_EQ(fields.detail, "unknown");
  EXPECT_EQ(onlyKeys(Json::array(), {}).error, StrictJsonError::NotObject);
}

/// Проверяет отсутствие неявных преобразований и полные границы целых типов.
TEST(StrictJson, ReadsRequiredNumericRanges)
{
  Json value = {{"negative", -1},
                {"uint64", std::numeric_limits<std::uint64_t>::max()},
                {"int64", std::numeric_limits<std::int64_t>::min()},
                {"tooWideForInt", static_cast<std::int64_t>(std::numeric_limits<int>::max()) + 1},
                {"text", "1"}};
  std::uint64_t unsignedValue = 0;
  std::int64_t signedValue = 0;
  int intValue = 0;
  EXPECT_EQ(readUint64(value, "negative", unsignedValue).error, StrictJsonError::OutOfRange);
  EXPECT_TRUE(readUint64(value, "uint64", unsignedValue));
  EXPECT_EQ(unsignedValue, std::numeric_limits<std::uint64_t>::max());
  EXPECT_TRUE(readInt64(value, "int64", signedValue));
  EXPECT_EQ(signedValue, std::numeric_limits<std::int64_t>::min());
  EXPECT_EQ(readInt(value, "tooWideForInt", intValue).error, StrictJsonError::OutOfRange);
  EXPECT_EQ(readInt(value, "text", intValue).error, StrictJsonError::WrongType);
  EXPECT_EQ(readInt(value, "missing", intValue).error, StrictJsonError::MissingField);
}

/// Проверяет конечность чисел и точную проверку корневого контракта.
TEST(StrictJson, ValidatesFiniteNumbersAndRootContract)
{
  Json value = {{"format", "example.contract"}, {"version", 2}, {"finite", 1.25}};
  double number = 0.0;
  int version = 0;
  EXPECT_TRUE(readDouble(value, "finite", number));
  EXPECT_DOUBLE_EQ(number, 1.25);
  value["infinite"] = std::numeric_limits<double>::infinity();
  EXPECT_EQ(readDouble(value, "infinite", number).error, StrictJsonError::NonFinite);
  EXPECT_TRUE(validateRootContract(value, "example.contract", {1, 2}, version));
  EXPECT_EQ(version, 2);
  EXPECT_EQ(validateRootContract(value, "other.contract", {2}, version).error, StrictJsonError::UnsupportedFormat);
  EXPECT_EQ(validateRootContract(value, "example.contract", {1}, version).error, StrictJsonError::UnsupportedVersion);
}

/// Проверяет чтение файла без преобразования содержимого и диагностируемый отказ открытия.
TEST(StrictJson, ReadsTextFileExactly)
{
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "aipackaging-strict-json-test.json";
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output << "{\r\n  \"value\": 1\r\n}\r\n";
  }
  std::string text;
  EXPECT_TRUE(readTextFile(path.string(), text));
  EXPECT_EQ(text, "{\r\n  \"value\": 1\r\n}\r\n");
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  EXPECT_EQ(readTextFile(path.string(), text).error, StrictJsonError::IoFailure);
}
