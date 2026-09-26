#include "strictjson.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace aipackaging::solver::internal
{
namespace
{
/// Различает отсутствие обязательного поля и наличие значения неверного типа.
StrictJsonResult requiredField(const Json & object, const char * key)
{
  if (!object.is_object())
    return {StrictJsonError::NotObject, key};
  if (!object.contains(key))
    return {StrictJsonError::MissingField, key};
  return {};
}
} // namespace

/// Вызывает синтаксический анализатор `nlohmann::json` и сохраняет его точное сообщение при отказе.
StrictJsonResult parseDocument(const std::string & text, Json & value)
{
  try
  {
    value = Json::parse(text);
    return {};
  }
  catch (const Json::exception & error)
  {
    return {StrictJsonError::InvalidSyntax, error.what()};
  }
}

/// Обходит фактические поля объекта и отвергает первое имя вне разрешённого множества.
StrictJsonResult onlyKeys(const Json & value, std::initializer_list<std::string_view> keys)
{
  if (!value.is_object())
    return {StrictJsonError::NotObject, {}};
  for (const auto & item : value.items())
  {
    const bool known = std::any_of(keys.begin(), keys.end(), [&item](std::string_view key) { return item.key() == key; });
    if (!known)
      return {StrictJsonError::UnknownField, item.key()};
  }
  return {};
}

/// Проверяет обязательность и строковый тип, затем копирует значение поля.
StrictJsonResult readString(const Json & object, const char * key, std::string & value)
{
  StrictJsonResult field = requiredField(object, key);
  if (!field)
    return field;
  if (!object.at(key).is_string())
    return {StrictJsonError::WrongType, key};
  value = object.at(key).get<std::string>();
  return {};
}

/// Проверяет числовой тип и конечность преобразованного значения.
StrictJsonResult readDouble(const Json & object, const char * key, double & value)
{
  StrictJsonResult field = requiredField(object, key);
  if (!field)
    return field;
  if (!object.at(key).is_number())
    return {StrictJsonError::WrongType, key};
  try
  {
    value = object.at(key).get<double>();
  }
  catch (const Json::exception &)
  {
    return {StrictJsonError::OutOfRange, key};
  }
  return std::isfinite(value) ? StrictJsonResult{} : StrictJsonResult{StrictJsonError::NonFinite, key};
}

/// Преобразует знаковое целое только после проверки границ типа `int`.
StrictJsonResult readInt(const Json & object, const char * key, int & value)
{
  std::int64_t parsed = 0;
  StrictJsonResult result = readInt64(object, key, parsed);
  if (!result)
    return result;
  if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
    return {StrictJsonError::OutOfRange, key};
  value = static_cast<int>(parsed);
  return {};
}

/// Проверяет целый тип и безопасно извлекает знаковое 64-битное значение.
StrictJsonResult readInt64(const Json & object, const char * key, std::int64_t & value)
{
  StrictJsonResult field = requiredField(object, key);
  if (!field)
    return field;
  if (!object.at(key).is_number_integer())
    return {StrictJsonError::WrongType, key};
  try
  {
    value = object.at(key).get<std::int64_t>();
    return {};
  }
  catch (const Json::exception &)
  {
    return {StrictJsonError::OutOfRange, key};
  }
}

/// Разрешает беззнаковое либо неотрицательное знаковое представление и контролирует диапазон `uint64_t`.
StrictJsonResult readUint64(const Json & object, const char * key, std::uint64_t & value)
{
  StrictJsonResult field = requiredField(object, key);
  if (!field)
    return field;
  if (!object.at(key).is_number_integer())
    return {StrictJsonError::WrongType, key};
  try
  {
    if (object.at(key).is_number_unsigned())
    {
      value = object.at(key).get<std::uint64_t>();
      return {};
    }
    const std::int64_t parsed = object.at(key).get<std::int64_t>();
    if (parsed < 0)
      return {StrictJsonError::OutOfRange, key};
    value = static_cast<std::uint64_t>(parsed);
    return {};
  }
  catch (const Json::exception &)
  {
    return {StrictJsonError::OutOfRange, key};
  }
}

/// Сужает проверенное беззнаковое значение только при представимости в `size_t`.
StrictJsonResult readSize(const Json & object, const char * key, std::size_t & value)
{
  std::uint64_t parsed = 0;
  StrictJsonResult result = readUint64(object, key, parsed);
  if (!result)
    return result;
  if (parsed > std::numeric_limits<std::size_t>::max())
    return {StrictJsonError::OutOfRange, key};
  value = static_cast<std::size_t>(parsed);
  return {};
}

/// Читает обязательные признаки корневого контракта и проверяет точное имя и множество версий.
StrictJsonResult validateRootContract(const Json & root, std::string_view expectedFormat,
                                      std::initializer_list<int> supportedVersions, int & version)
{
  std::string format;
  StrictJsonResult result = readString(root, "format", format);
  if (!result)
    return result;
  if (format != expectedFormat)
    return {StrictJsonError::UnsupportedFormat, format};
  result = readInt(root, "version", version);
  if (!result)
    return result;
  if (std::find(supportedVersions.begin(), supportedVersions.end(), version) == supportedVersions.end())
    return {StrictJsonError::UnsupportedVersion, std::to_string(version)};
  return {};
}

/// Читает байты файла без преобразования переводов строк и проверяет завершение потока.
StrictJsonResult readTextFile(const std::string & filePath, std::string & text)
{
  std::ifstream input(filePath, std::ios::binary);
  if (!input)
    return {StrictJsonError::IoFailure, filePath};
  text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  if (!input && !input.eof())
    return {StrictJsonError::IoFailure, filePath};
  return {};
}
} // namespace aipackaging::solver::internal
