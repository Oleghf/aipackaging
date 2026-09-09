#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

#include <io/packingsceneloader.h>

namespace
{
struct JsonValue
{
  enum class Type
  {
    Object,
    Array,
    String,
    Number
  };

  Type type = Type::Object;
  std::map<std::string, JsonValue> object;
  std::vector<JsonValue> array;
  std::string string;
  double number = 0.0;
};

class JsonParser
{
public:
  explicit JsonParser(const std::string & text)
    : text_(text)
  {
  }

  bool parse(JsonValue & value)
  {
    skipWhitespace();
    if (!parseValue(value))
      return false;

    skipWhitespace();
    return pos_ == text_.size();
  }

private:
  bool parseValue(JsonValue & value)
  {
    skipWhitespace();
    if (pos_ >= text_.size())
      return false;

    switch (text_[pos_])
    {
      case '{':
        return parseObject(value);
      case '[':
        return parseArray(value);
      case '"':
        value.type = JsonValue::Type::String;
        return parseString(value.string);
      default:
        value.type = JsonValue::Type::Number;
        return parseNumber(value.number);
    }
  }

  bool parseObject(JsonValue & value)
  {
    if (!consume('{'))
      return false;

    value.type = JsonValue::Type::Object;
    value.object.clear();
    skipWhitespace();

    if (consume('}'))
      return true;

    while (true)
    {
      std::string key;
      if (!parseString(key))
        return false;
      if (!consume(':'))
        return false;

      JsonValue child;
      if (!parseValue(child))
        return false;

      value.object.emplace(std::move(key), std::move(child));

      if (consume('}'))
        return true;
      if (!consume(','))
        return false;
    }
  }

  bool parseArray(JsonValue & value)
  {
    if (!consume('['))
      return false;

    value.type = JsonValue::Type::Array;
    value.array.clear();
    skipWhitespace();

    if (consume(']'))
      return true;

    while (true)
    {
      JsonValue child;
      if (!parseValue(child))
        return false;

      value.array.push_back(std::move(child));

      if (consume(']'))
        return true;
      if (!consume(','))
        return false;
    }
  }

  bool parseString(std::string & value)
  {
    if (!consume('"'))
      return false;

    value.clear();
    while (pos_ < text_.size())
    {
      const char ch = text_[pos_++];
      if (ch == '"')
        return true;

      if (ch != '\\')
      {
        value.push_back(ch);
        continue;
      }

      if (pos_ >= text_.size())
        return false;

      const char escaped = text_[pos_++];
      switch (escaped)
      {
        case '"':
        case '\\':
        case '/':
          value.push_back(escaped);
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        default:
          return false;
      }
    }

    return false;
  }

  bool parseNumber(double & value)
  {
    const char * begin = text_.c_str() + pos_;
    char * end = nullptr;
    errno = 0;
    value = std::strtod(begin, &end);
    if (end == begin || errno == ERANGE)
      return false;

    // Загрузчик принимает числа только после проверок конечности, диапазона и целочисленности.
    pos_ += static_cast<size_t>(end - begin);
    return true;
  }

  bool consume(char expected)
  {
    skipWhitespace();
    if (pos_ >= text_.size() || text_[pos_] != expected)
      return false;

    ++pos_;
    return true;
  }

  void skipWhitespace()
  {
    while (pos_ < text_.size())
    {
      const char ch = text_[pos_];
      if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t')
        break;
      ++pos_;
    }
  }

private:
  const std::string & text_;
  size_t pos_ = 0;
};

const JsonValue * member(const JsonValue & value, const std::string & name)
{
  if (value.type != JsonValue::Type::Object)
    return nullptr;

  const auto it = value.object.find(name);
  if (it == value.object.end())
    return nullptr;

  return &it->second;
}

bool stringMember(const JsonValue & value, const std::string & name, std::string & result)
{
  const JsonValue * found = member(value, name);
  if (!found || found->type != JsonValue::Type::String)
    return false;

  result = found->string;
  return true;
}

bool numberMember(const JsonValue & value, const std::string & name, double & result)
{
  const JsonValue * found = member(value, name);
  if (!found || found->type != JsonValue::Type::Number)
    return false;

  result = found->number;
  return true;
}

bool objectMember(const JsonValue & value, const std::string & name, const JsonValue *& result)
{
  result = member(value, name);
  return result && result->type == JsonValue::Type::Object;
}

bool arrayMember(const JsonValue & value, const std::string & name, const JsonValue *& result)
{
  result = member(value, name);
  return result && result->type == JsonValue::Type::Array;
}

bool isInteger(double value)
{
  return std::isfinite(value) && std::floor(value) == value;
}

bool canReadInt(double value)
{
  return isInteger(value) && value >= static_cast<double>(std::numeric_limits<int>::min()) &&
         value <= static_cast<double>(std::numeric_limits<int>::max());
}

bool canReadSize(double value)
{
  return isInteger(value) && value >= 0.0 && value <= static_cast<double>(std::numeric_limits<size_t>::max());
}

bool readIntegerMember(const JsonValue & value, const std::string & name, int & result)
{
  double number = 0.0;
  if (!numberMember(value, name, number) || !canReadInt(number))
    return false;

  result = static_cast<int>(number);
  return true;
}

bool readSizeMember(const JsonValue & value, const std::string & name, size_t & result)
{
  double number = 0.0;
  if (!numberMember(value, name, number) || !canReadSize(number))
    return false;

  result = static_cast<size_t>(number);
  return true;
}

PackingSceneLoader::Result fail(const std::string & message)
{
  return {false, {}, message};
}
} // namespace


//------------------------------------------------------------------------------
/**
  Загружает JSON-сцену упаковки из файла
*/
//--
PackingSceneLoader::Result PackingSceneLoader::loadFromFile(const std::string & filePath)
{
  std::ifstream input(filePath);
  if (!input.is_open())
    return fail("Не удалось открыть файл сцены упаковки");

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return loadFromText(buffer.str());
}


//------------------------------------------------------------------------------
/**
  Загружает JSON-сцену упаковки из текста
*/
//--
PackingSceneLoader::Result PackingSceneLoader::loadFromText(const std::string & text)
{
  JsonValue root;
  JsonParser parser(text);
  if (!parser.parse(root) || root.type != JsonValue::Type::Object)
    return fail("Некорректный JSON сцены упаковки");

  std::string format;
  if (!stringMember(root, "format", format) || format != "aipackaging.packing_scene")
    return fail("Файл не является сценой упаковки AIPackaging");

  double version = 0.0;
  if (!numberMember(root, "version", version) || version != 1.0)
    return fail("Неподдерживаемая версия сцены упаковки");

  const JsonValue * board = nullptr;
  if (!objectMember(root, "board", board))
    return fail("В сцене отсутствует описание доски");

  PackingSceneSnapshot snapshot;
  if (!readSizeMember(*board, "columns", snapshot.board.columns) || !readSizeMember(*board, "rows", snapshot.board.rows))
  {
    return fail("Некорректный размер доски в сцене упаковки");
  }

  std::string unit;
  if (stringMember(*board, "unit", unit))
    snapshot.board.unit = unit;

  const JsonValue * objects = nullptr;
  if (!arrayMember(root, "objects", objects))
    return fail("В сцене отсутствует список объектов");

  std::set<std::string> objectIds;
  for (const JsonValue & objectValue : objects->array)
  {
    if (objectValue.type != JsonValue::Type::Object)
      return fail("Некорректное описание объекта сцены");

    PackingSceneObject object;
    if (!stringMember(objectValue, "id", object.id) || object.id.empty())
      return fail("Объект сцены должен иметь id");

    if (!objectIds.insert(object.id).second)
      return fail("В сцене есть дублирующийся id объекта");

    const JsonValue * geometry = nullptr;
    if (!objectMember(objectValue, "geometry", geometry))
      return fail("Объект сцены должен иметь geometry");

    std::string geometryType;
    if (!stringMember(*geometry, "type", geometryType) || geometryType != "cell_grid")
      return fail("Поддерживается только geometry.type = cell_grid");

    const JsonValue * cells = nullptr;
    if (!arrayMember(*geometry, "cells", cells))
      return fail("Геометрия объекта должна содержать cells");

    for (const JsonValue & cellValue : cells->array)
    {
      if (cellValue.type != JsonValue::Type::Object)
        return fail("Некорректная клетка объекта сцены");

      PackingSceneCell cell;
      if (!readIntegerMember(cellValue, "column", cell.column) || !readIntegerMember(cellValue, "row", cell.row))
      {
        return fail("Клетка объекта должна иметь целые column и row");
      }

      object.cells.push_back(cell);
    }

    snapshot.objects.push_back(std::move(object));
  }

  const JsonValue * actions = nullptr;
  if (!arrayMember(root, "actions", actions))
    return fail("В сцене отсутствует список действий");

  for (const JsonValue & actionValue : actions->array)
  {
    if (actionValue.type != JsonValue::Type::Object)
      return fail("Некорректное действие сцены");

    PackingSceneAction action;
    if (!stringMember(actionValue, "type", action.type) || action.type != "place")
      return fail("Поддерживается только действие place");

    if (!stringMember(actionValue, "objectId", action.objectId) || !objectIds.contains(action.objectId))
      return fail("Действие сцены ссылается на неизвестный объект");

    if (!numberMember(actionValue, "x", action.x) || !numberMember(actionValue, "y", action.y) || !canReadInt(action.x) ||
        !canReadInt(action.y))
    {
      return fail("Координаты place action должны быть целыми cell units");
    }

    if (!numberMember(actionValue, "rotationDegrees", action.rotationDegrees) || action.rotationDegrees != 0.0)
      return fail("Загрузка сцены поддерживает только rotationDegrees = 0.0");

    snapshot.actions.push_back(action);
  }

  return {true, std::move(snapshot), {}};
}
