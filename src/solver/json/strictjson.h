#ifndef AIPACKAGING_SOLVER_JSON_STRICTJSON_H
#define AIPACKAGING_SOLVER_JSON_STRICTJSON_H

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace aipackaging::solver::internal
{
using Json = nlohmann::json;

/// Категория отказа закрытой операции строгого чтения JSON.
enum class StrictJsonError : std::uint8_t
{
  None,
  InvalidSyntax,
  NotObject,
  UnknownField,
  MissingField,
  WrongType,
  OutOfRange,
  NonFinite,
  UnsupportedFormat,
  UnsupportedVersion,
  IoFailure
};

/// Структурированный результат операции чтения без предметной диагностической строки.
struct StrictJsonResult
{
  StrictJsonError error = StrictJsonError::None;
  std::string detail;

  /// Позволяет использовать успешность результата в условиях без потери категории ошибки.
  explicit operator bool() const noexcept { return error == StrictJsonError::None; }
};

/// Разбирает JSON-текст, сохраняя исходную диагностику синтаксического анализатора.
StrictJsonResult parseDocument(const std::string & text, Json & value);
/// Проверяет, что значение является объектом и не содержит неизвестных полей.
StrictJsonResult onlyKeys(const Json & value, std::initializer_list<std::string_view> keys);
/// Читает обязательное строковое поле без неявного преобразования типа.
StrictJsonResult readString(const Json & object, const char * key, std::string & value);
/// Читает обязательное конечное число двойной точности.
StrictJsonResult readDouble(const Json & object, const char * key, double & value);
/// Читает обязательное целое число в диапазоне типа `int`.
StrictJsonResult readInt(const Json & object, const char * key, int & value);
/// Читает обязательное целое число в диапазоне типа `int64_t`.
StrictJsonResult readInt64(const Json & object, const char * key, std::int64_t & value);
/// Читает обязательное неотрицательное целое число в диапазоне типа `uint64_t`.
StrictJsonResult readUint64(const Json & object, const char * key, std::uint64_t & value);
/// Читает обязательный размер с учётом диапазона `size_t` текущей платформы.
StrictJsonResult readSize(const Json & object, const char * key, std::size_t & value);
/// Проверяет корневые поля `format` и `version` относительно ожидаемого контракта.
StrictJsonResult validateRootContract(const Json & root, std::string_view expectedFormat,
                                      std::initializer_list<int> supportedVersions, int & version);
/// Читает файл целиком в двоичном режиме либо возвращает структурированный отказ ввода-вывода.
StrictJsonResult readTextFile(const std::string & filePath, std::string & text);
} // namespace aipackaging::solver::internal

#endif
