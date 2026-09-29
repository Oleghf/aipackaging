#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <locale>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <aipackaging/import/dxf_import.h>

namespace aipackaging::dxf
{
namespace
{
constexpr double MAX_ABS_COORDINATE_MM = 9.0e12;
constexpr double POINT_EPSILON = 1.0e-12;

struct Pair
{
  int code = 0;
  std::string value;
  std::size_t line = 0;
};

struct Record
{
  std::string type;
  std::string section;
  std::size_t line = 0;
  std::vector<Pair> fields;
};

struct Point
{
  double x = 0.0;
  double y = 0.0;
};

enum class SegmentKind : std::uint8_t
{
  Line,
  Arc,
  CubicBezier
};

struct Segment
{
  SegmentKind kind = SegmentKind::Line;
  Point start;
  Point end;
  Point center;
  Point control1;
  Point control2;
  bool clockwise = false;
  std::string layer;
  std::size_t sourceOrder = 0;
  std::size_t line = 0;
};

struct Chain
{
  std::uint64_t id = 0;
  std::string layer;
  std::vector<Segment> segments;
  bool closed = false;
};

struct ParsedDxf
{
  bool success = false;
  int acadVersion = 0;
  LengthUnit unit = LengthUnit::Unknown;
  std::vector<Record> entities;
  std::vector<LayerSummary> layers;
  std::vector<Diagnostic> diagnostics;
};

struct Geometry
{
  bool success = false;
  std::vector<Chain> chains;
  std::vector<JoinProposal> proposals;
  std::vector<Diagnostic> diagnostics;
};

/// Удаляет пробелы вокруг значения DXF, не изменяя его внутреннее содержимое.
std::string trim(std::string_view value)
{
  std::size_t begin = 0;
  while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r'))
    ++begin;
  std::size_t end = value.size();
  while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r'))
    --end;
  return std::string(value.substr(begin, end - begin));
}

/// Строго преобразует десятичное целое без принятия лишних символов.
template<typename Integer>
bool parseInteger(std::string_view text, Integer & value)
{
  const std::string cleaned = trim(text);
  if (cleaned.empty())
    return false;
  const auto [end, error] = std::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), value);
  return error == std::errc{} && end == cleaned.data() + cleaned.size();
}

/// Строго преобразует конечное вещественное число без зависимости от локали процесса.
bool parseDouble(std::string_view text, double & value)
{
  const std::string cleaned = trim(text);
  if (cleaned.empty())
    return false;
  const auto [end, error] = std::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), value, std::chars_format::general);
  return error == std::errc{} && end == cleaned.data() + cleaned.size() && std::isfinite(value);
}

/// Разбивает ASCII DXF на обязательные пары строк «код — значение».
bool tokenize(std::string_view contents, std::vector<Pair> & pairs, std::vector<Diagnostic> & diagnostics)
{
  if (contents.starts_with("AutoCAD Binary DXF"))
  {
    diagnostics.push_back({DiagnosticSeverity::Error, 1, {}, {}, "двоичный DXF не поддерживается"});
    return false;
  }
  std::vector<std::string_view> lines;
  std::size_t offset = 0;
  while (offset < contents.size())
  {
    const std::size_t end = contents.find('\n', offset);
    lines.push_back(contents.substr(offset, end == std::string_view::npos ? contents.size() - offset : end - offset));
    if (end == std::string_view::npos)
      break;
    offset = end + 1;
  }
  if (lines.empty() || lines.size() % 2 != 0)
  {
    diagnostics.push_back(
      {DiagnosticSeverity::Error, lines.empty() ? 1U : lines.size(), {}, {}, "DXF содержит незавершённую пару кода и значения"});
    return false;
  }
  pairs.reserve(lines.size() / 2);
  for (std::size_t index = 0; index < lines.size(); index += 2)
  {
    int code = 0;
    if (!parseInteger(lines[index], code))
    {
      diagnostics.push_back({DiagnosticSeverity::Error, index + 1, {}, {}, "код группы DXF должен быть целым числом"});
      return false;
    }
    pairs.push_back({code, trim(lines[index + 1]), index + 1});
  }
  return true;
}

/// Сопоставляет стандартный код `$INSUNITS` с поддерживаемой единицей длины.
LengthUnit unitFromCode(int code) noexcept
{
  switch (code)
  {
    case 1:
      return LengthUnit::Inch;
    case 2:
      return LengthUnit::Foot;
    case 4:
      return LengthUnit::Millimeter;
    case 5:
      return LengthUnit::Centimeter;
    case 6:
      return LengthUnit::Meter;
    case 7:
      return LengthUnit::Kilometer;
    case 10:
      return LengthUnit::Yard;
    case 13:
      return LengthUnit::Micrometer;
    default:
      return LengthUnit::Unknown;
  }
}

/// Возвращает множитель перевода выбранной единицы в миллиметры.
double unitFactor(LengthUnit unit) noexcept
{
  switch (unit)
  {
    case LengthUnit::Millimeter:
      return 1.0;
    case LengthUnit::Centimeter:
      return 10.0;
    case LengthUnit::Meter:
      return 1000.0;
    case LengthUnit::Kilometer:
      return 1'000'000.0;
    case LengthUnit::Inch:
      return 25.4;
    case LengthUnit::Foot:
      return 304.8;
    case LengthUnit::Yard:
      return 914.4;
    case LengthUnit::Micrometer:
      return 0.001;
    case LengthUnit::Unknown:
      return 0.0;
  }
  return 0.0;
}

/// Формирует записи разделов, сохраняя исходную строку каждого объекта.
ParsedDxf parseDxf(std::string_view contents)
{
  ParsedDxf result;
  std::vector<Pair> pairs;
  if (!tokenize(contents, pairs, result.diagnostics))
    return result;

  std::string section;
  for (std::size_t index = 0; index < pairs.size();)
  {
    if (pairs[index].code == 0 && pairs[index].value == "SECTION")
    {
      if (index + 1 >= pairs.size() || pairs[index + 1].code != 2)
      {
        result.diagnostics.push_back({DiagnosticSeverity::Error, pairs[index].line, {}, {}, "раздел DXF не содержит имени"});
        return result;
      }
      section = pairs[index + 1].value;
      index += 2;
      continue;
    }
    if (pairs[index].code == 0 && pairs[index].value == "ENDSEC")
    {
      section.clear();
      ++index;
      continue;
    }
    if (pairs[index].code == 0 && pairs[index].value == "EOF")
      break;

    if (section == "HEADER" && pairs[index].code == 9)
    {
      if (pairs[index].value == "$INSUNITS" && index + 1 < pairs.size() && pairs[index + 1].code == 70)
      {
        int code = 0;
        if (parseInteger(pairs[index + 1].value, code))
          result.unit = unitFromCode(code);
      }
      else if (pairs[index].value == "$ACADVER" && index + 1 < pairs.size() && pairs[index + 1].code == 1)
      {
        const std::string & version = pairs[index + 1].value;
        if (version.starts_with("AC") && version.size() > 2)
          parseInteger(std::string_view(version).substr(2), result.acadVersion);
      }
    }

    if (section == "ENTITIES" && pairs[index].code == 0)
    {
      Record record;
      record.type = pairs[index].value;
      record.section = section;
      record.line = pairs[index].line;
      ++index;
      while (index < pairs.size() && pairs[index].code != 0)
        record.fields.push_back(pairs[index++]);
      result.entities.push_back(std::move(record));
      continue;
    }
    ++index;
  }

  if (result.entities.empty())
  {
    result.diagnostics.push_back({DiagnosticSeverity::Error, 0, {}, {}, "раздел ENTITIES отсутствует или пуст"});
    return result;
  }
  result.success = true;
  return result;
}

/// Возвращает первое значение требуемого кода поля записи.
const Pair * field(const Record & record, int code) noexcept
{
  const auto found =
    std::find_if(record.fields.begin(), record.fields.end(), [code](const Pair & item) { return item.code == code; });
  return found == record.fields.end() ? nullptr : &*found;
}

/// Возвращает имя слоя сущности либо стандартный слой `0`.
std::string layerOf(const Record & record)
{
  if (const Pair * layer = field(record, 8); layer && !layer->value.empty())
    return layer->value;
  return "0";
}

/// Читает обязательное конечное поле вещественного типа и добавляет предметную диагностику.
bool requireDouble(const Record & record, int code, double & value, std::vector<Diagnostic> & diagnostics, std::string_view name)
{
  const Pair * source = field(record, code);
  if (!source || !parseDouble(source->value, value))
  {
    diagnostics.push_back({DiagnosticSeverity::Error, source ? source->line : record.line, layerOf(record), record.type,
                           "поле " + std::string(name) + " отсутствует или содержит нечисловое значение"});
    return false;
  }
  return true;
}

/// Проверяет плоскостность сущности по указанным кодам координаты Z.
bool planar(const Record & record, std::initializer_list<int> codes, std::vector<Diagnostic> & diagnostics)
{
  for (int code : codes)
  {
    for (const Pair & item : record.fields)
    {
      if (item.code != code)
        continue;
      double z = 0.0;
      if (!parseDouble(item.value, z) || std::abs(z) > POINT_EPSILON)
      {
        diagnostics.push_back(
          {DiagnosticSeverity::Warning, item.line, layerOf(record), record.type, "трёхмерная геометрия не поддерживается"});
        return false;
      }
    }
  }
  return true;
}

/// Безопасно переводит точку в миллиметры и проверяет рабочий диапазон.
bool scaledPoint(double x, double y, double factor, Point & result, const Record & record, std::vector<Diagnostic> & diagnostics)
{
  const long double scaledX = static_cast<long double>(x) * factor;
  const long double scaledY = static_cast<long double>(y) * factor;
  if (!std::isfinite(scaledX) || !std::isfinite(scaledY) || std::abs(scaledX) > MAX_ABS_COORDINATE_MM ||
      std::abs(scaledY) > MAX_ABS_COORDINATE_MM)
  {
    diagnostics.push_back({DiagnosticSeverity::Error, record.line, layerOf(record), record.type,
                           "координаты сущности выходят за поддерживаемый диапазон"});
    return false;
  }
  result = {static_cast<double>(scaledX), static_cast<double>(scaledY)};
  return true;
}

/// Сравнивает точки точно после нормализации отрицательного нуля.
bool samePoint(Point lhs, Point rhs) noexcept
{
  if (lhs.x == 0.0)
    lhs.x = 0.0;
  if (lhs.y == 0.0)
    lhs.y = 0.0;
  if (rhs.x == 0.0)
    rhs.x = 0.0;
  if (rhs.y == 0.0)
    rhs.y = 0.0;
  return lhs.x == rhs.x && lhs.y == rhs.y;
}

/// Возвращает евклидово расстояние между двумя конечными точками.
double distance(Point lhs, Point rhs) noexcept
{
  return std::hypot(lhs.x - rhs.x, lhs.y - rhs.y);
}

/// Создаёт дугу по коэффициенту выпуклости полилинии.
bool bulgeArc(Point start, Point end, double bulge, Segment & segment) noexcept
{
  if (!std::isfinite(bulge) || std::abs(bulge) <= POINT_EPSILON || samePoint(start, end))
    return false;
  const double chordX = end.x - start.x;
  const double chordY = end.y - start.y;
  const double chord = std::hypot(chordX, chordY);
  const double offset = chord * (1.0 - bulge * bulge) / (4.0 * bulge);
  const Point middle{(start.x + end.x) * 0.5, (start.y + end.y) * 0.5};
  segment.kind = SegmentKind::Arc;
  segment.start = start;
  segment.end = end;
  segment.center = {middle.x - chordY / chord * offset, middle.y + chordX / chord * offset};
  segment.clockwise = bulge < 0.0;
  return std::isfinite(segment.center.x) && std::isfinite(segment.center.y);
}

/// Добавляет сегменты одной полилинии с учётом её замкнутости и коэффициентов выпуклости.
void appendPolyline(const std::vector<Point> & points, const std::vector<double> & bulges, bool closed, std::string layer,
                    std::size_t sourceOrder, std::size_t line, std::vector<Segment> & segments)
{
  if (points.size() < 2)
    return;
  const std::size_t count = closed ? points.size() : points.size() - 1;
  for (std::size_t index = 0; index < count; ++index)
  {
    const Point start = points[index];
    const Point end = points[(index + 1) % points.size()];
    Segment segment;
    const double bulge = index < bulges.size() ? bulges[index] : 0.0;
    if (!bulgeArc(start, end, bulge, segment))
    {
      segment.kind = SegmentKind::Line;
      segment.start = start;
      segment.end = end;
    }
    segment.layer = layer;
    segment.sourceOrder = sourceOrder * 100000 + index;
    segment.line = line;
    segments.push_back(std::move(segment));
  }
}

/// Читает `LINE` и формирует один ориентированный отрезок.
void convertLine(const Record & record, double factor, std::size_t order, std::vector<Segment> & segments,
                 std::vector<Diagnostic> & diagnostics)
{
  if (!planar(record, {30, 31}, diagnostics))
    return;
  double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
  if (!requireDouble(record, 10, x1, diagnostics, "X начала") || !requireDouble(record, 20, y1, diagnostics, "Y начала") ||
      !requireDouble(record, 11, x2, diagnostics, "X конца") || !requireDouble(record, 21, y2, diagnostics, "Y конца"))
    return;
  Segment segment;
  if (!scaledPoint(x1, y1, factor, segment.start, record, diagnostics) ||
      !scaledPoint(x2, y2, factor, segment.end, record, diagnostics))
    return;
  segment.layer = layerOf(record);
  segment.sourceOrder = order * 100000;
  segment.line = record.line;
  segments.push_back(std::move(segment));
}

/// Читает вершины `LWPOLYLINE` в исходном порядке DXF.
void convertLightPolyline(const Record & record, double factor, std::size_t order, std::vector<Segment> & segments,
                          std::vector<Diagnostic> & diagnostics)
{
  if (!planar(record, {30, 38}, diagnostics))
    return;
  std::vector<Point> points;
  std::vector<double> bulges;
  for (std::size_t index = 0; index < record.fields.size(); ++index)
  {
    if (record.fields[index].code != 10)
      continue;
    double x = 0.0;
    if (!parseDouble(record.fields[index].value, x))
      continue;
    double y = 0.0;
    double bulge = 0.0;
    bool hasY = false;
    for (std::size_t cursor = index + 1; cursor < record.fields.size() && record.fields[cursor].code != 10; ++cursor)
    {
      if (record.fields[cursor].code == 20)
        hasY = parseDouble(record.fields[cursor].value, y);
      else if (record.fields[cursor].code == 42)
        parseDouble(record.fields[cursor].value, bulge);
    }
    Point point;
    if (!hasY || !scaledPoint(x, y, factor, point, record, diagnostics))
    {
      diagnostics.push_back({DiagnosticSeverity::Error, record.fields[index].line, layerOf(record), record.type,
                             "вершина полилинии не содержит корректную пару координат"});
      return;
    }
    points.push_back(point);
    bulges.push_back(bulge);
  }
  int flags = 0;
  if (const Pair * source = field(record, 70))
    parseInteger(source->value, flags);
  if (points.size() < 2)
  {
    diagnostics.push_back(
      {DiagnosticSeverity::Error, record.line, layerOf(record), record.type, "полилиния должна содержать не менее двух вершин"});
    return;
  }
  appendPolyline(points, bulges, (flags & 1) != 0, layerOf(record), order, record.line, segments);
}

/// Читает обычную двумерную `POLYLINE` вместе с последующими `VERTEX`.
std::size_t convertPolyline(const std::vector<Record> & records, std::size_t index, double factor,
                            std::vector<Segment> & segments, std::vector<Diagnostic> & diagnostics)
{
  const Record & header = records[index];
  int flags = 0;
  if (const Pair * source = field(header, 70))
    parseInteger(source->value, flags);
  if ((flags & (8 | 16 | 64)) != 0 || !planar(header, {30}, diagnostics))
  {
    diagnostics.push_back(
      {DiagnosticSeverity::Warning, header.line, layerOf(header), header.type, "поддерживаются только двумерные полилинии"});
  }
  std::vector<Point> points;
  std::vector<double> bulges;
  std::size_t cursor = index + 1;
  for (; cursor < records.size() && records[cursor].type != "SEQEND"; ++cursor)
  {
    if (records[cursor].type != "VERTEX")
      break;
    const Record & vertex = records[cursor];
    if (!planar(vertex, {30}, diagnostics))
      continue;
    double x = 0.0, y = 0.0;
    if (!requireDouble(vertex, 10, x, diagnostics, "X вершины") || !requireDouble(vertex, 20, y, diagnostics, "Y вершины"))
      continue;
    Point point;
    if (!scaledPoint(x, y, factor, point, vertex, diagnostics))
      continue;
    double bulge = 0.0;
    if (const Pair * source = field(vertex, 42))
      parseDouble(source->value, bulge);
    points.push_back(point);
    bulges.push_back(bulge);
  }
  if (cursor >= records.size() || records[cursor].type != "SEQEND")
  {
    diagnostics.push_back(
      {DiagnosticSeverity::Error, header.line, layerOf(header), header.type, "POLYLINE не завершена записью SEQEND"});
    return cursor == 0 ? index : cursor - 1;
  }
  appendPolyline(points, bulges, (flags & 1) != 0, layerOf(header), index, header.line, segments);
  return cursor;
}

/// Читает дугу DXF, направление которой всегда задаётся против часовой стрелки.
void convertArc(const Record & record, double factor, std::size_t order, std::vector<Segment> & segments,
                std::vector<Diagnostic> & diagnostics)
{
  if (!planar(record, {30}, diagnostics))
    return;
  double x = 0.0, y = 0.0, radius = 0.0, startDegrees = 0.0, endDegrees = 0.0;
  if (!requireDouble(record, 10, x, diagnostics, "X центра") || !requireDouble(record, 20, y, diagnostics, "Y центра") ||
      !requireDouble(record, 40, radius, diagnostics, "радиус") ||
      !requireDouble(record, 50, startDegrees, diagnostics, "начальный угол") ||
      !requireDouble(record, 51, endDegrees, diagnostics, "конечный угол"))
    return;
  if (radius <= 0.0)
  {
    diagnostics.push_back(
      {DiagnosticSeverity::Error, record.line, layerOf(record), record.type, "радиус дуги должен быть положительным"});
    return;
  }
  Point center;
  if (!scaledPoint(x, y, factor, center, record, diagnostics))
    return;
  const double scaledRadius = radius * factor;
  const double start = startDegrees * std::numbers::pi / 180.0;
  const double end = endDegrees * std::numbers::pi / 180.0;
  Segment segment;
  segment.kind = SegmentKind::Arc;
  segment.center = center;
  segment.start = {center.x + scaledRadius * std::cos(start), center.y + scaledRadius * std::sin(start)};
  segment.end = {center.x + scaledRadius * std::cos(end), center.y + scaledRadius * std::sin(end)};
  segment.layer = layerOf(record);
  segment.sourceOrder = order * 100000;
  segment.line = record.line;
  segments.push_back(std::move(segment));
}

/// Читает окружность как две полуокружности с устойчивой правой начальной точкой.
void convertCircle(const Record & record, double factor, std::size_t order, std::vector<Segment> & segments,
                   std::vector<Diagnostic> & diagnostics)
{
  if (!planar(record, {30}, diagnostics))
    return;
  double x = 0.0, y = 0.0, radius = 0.0;
  if (!requireDouble(record, 10, x, diagnostics, "X центра") || !requireDouble(record, 20, y, diagnostics, "Y центра") ||
      !requireDouble(record, 40, radius, diagnostics, "радиус"))
    return;
  if (radius <= 0.0)
  {
    diagnostics.push_back(
      {DiagnosticSeverity::Error, record.line, layerOf(record), record.type, "радиус окружности должен быть положительным"});
    return;
  }
  Point center;
  if (!scaledPoint(x, y, factor, center, record, diagnostics))
    return;
  const double scaledRadius = radius * factor;
  Segment upper;
  upper.kind = SegmentKind::Arc;
  upper.start = {center.x + scaledRadius, center.y};
  upper.end = {center.x - scaledRadius, center.y};
  upper.center = center;
  upper.layer = layerOf(record);
  upper.sourceOrder = order * 100000;
  upper.line = record.line;
  Segment lower = upper;
  lower.start = upper.end;
  lower.end = upper.start;
  lower.sourceOrder++;
  segments.push_back(std::move(upper));
  segments.push_back(std::move(lower));
}

/// Находит интервал узлов, содержащий значение для вставки в B-сплайн.
std::size_t findSpan(const std::vector<double> & knots, std::size_t degree, std::size_t controlCount, double value)
{
  const std::size_t n = controlCount - 1;
  if (value >= knots[n + 1])
    return n;
  std::size_t low = degree;
  std::size_t high = n + 1;
  while (high - low > 1)
  {
    const std::size_t middle = (low + high) / 2;
    if (value < knots[middle])
      high = middle;
    else
      low = middle;
  }
  return low;
}

/// Вставляет один внутренний узел кубического B-сплайна без изменения его формы.
bool insertKnotOnce(std::vector<Point> & controls, std::vector<double> & knots, std::size_t degree, double value)
{
  const std::size_t n = controls.size() - 1;
  const std::size_t k = findSpan(knots, degree, controls.size(), value);
  const std::size_t multiplicity = static_cast<std::size_t>(std::count(knots.begin(), knots.end(), value));
  if (multiplicity >= degree)
    return true;
  std::vector<Point> nextControls(controls.size() + 1);
  std::vector<double> nextKnots(knots.size() + 1);
  for (std::size_t index = 0; index <= k; ++index)
    nextKnots[index] = knots[index];
  nextKnots[k + 1] = value;
  for (std::size_t index = k + 1; index < knots.size(); ++index)
    nextKnots[index + 1] = knots[index];
  for (std::size_t index = 0; index <= k - degree; ++index)
    nextControls[index] = controls[index];
  for (std::size_t index = k - multiplicity; index <= n; ++index)
    nextControls[index + 1] = controls[index];
  for (std::size_t index = k - degree + 1; index <= k - multiplicity; ++index)
  {
    const double denominator = knots[index + degree] - knots[index];
    if (!(denominator > 0.0))
      return false;
    const double alpha = (value - knots[index]) / denominator;
    nextControls[index] = {(1.0 - alpha) * controls[index - 1].x + alpha * controls[index].x,
                           (1.0 - alpha) * controls[index - 1].y + alpha * controls[index].y};
  }
  controls = std::move(nextControls);
  knots = std::move(nextKnots);
  return true;
}

/// Преобразует поддерживаемый кубический B-сплайн в последовательность Bézier.
void convertSpline(const Record & record, double factor, std::size_t order, std::vector<Segment> & segments,
                   std::vector<Diagnostic> & diagnostics)
{
  int flags = 0;
  int degree = 0;
  if (const Pair * source = field(record, 70))
    parseInteger(source->value, flags);
  if (const Pair * source = field(record, 71))
    parseInteger(source->value, degree);
  if (degree != 3 || (flags & (1 | 2 | 4 | 8)) != 0 || !planar(record, {30}, diagnostics))
  {
    diagnostics.push_back({DiagnosticSeverity::Warning, record.line, layerOf(record), record.type,
                           "поддерживаются только плоские нерациональные непериодические SPLINE степени 3"});
    return;
  }
  std::vector<double> knots;
  std::vector<double> weights;
  std::vector<Point> controls;
  for (std::size_t index = 0; index < record.fields.size(); ++index)
  {
    const Pair & item = record.fields[index];
    if (item.code == 40)
    {
      double value = 0.0;
      if (!parseDouble(item.value, value))
        return;
      knots.push_back(value);
    }
    else if (item.code == 41)
    {
      double value = 0.0;
      if (!parseDouble(item.value, value))
        return;
      weights.push_back(value);
    }
    else if (item.code == 10)
    {
      double x = 0.0, y = 0.0;
      if (!parseDouble(item.value, x))
        return;
      bool hasY = false;
      for (std::size_t cursor = index + 1; cursor < record.fields.size() && record.fields[cursor].code != 10; ++cursor)
      {
        if (record.fields[cursor].code == 20)
          hasY = parseDouble(record.fields[cursor].value, y);
      }
      Point point;
      if (!hasY || !scaledPoint(x, y, factor, point, record, diagnostics))
        return;
      controls.push_back(point);
    }
  }
  if (!weights.empty() && std::any_of(weights.begin(), weights.end(), [](double value) { return value != 1.0; }))
  {
    diagnostics.push_back(
      {DiagnosticSeverity::Warning, record.line, layerOf(record), record.type, "рациональный SPLINE не поддерживается"});
    return;
  }
  if (controls.size() < 4 || knots.size() != controls.size() + 4 || !std::is_sorted(knots.begin(), knots.end()))
  {
    diagnostics.push_back(
      {DiagnosticSeverity::Error, record.line, layerOf(record), record.type, "узлы и управляющие точки SPLINE несогласованы"});
    return;
  }
  if (!(knots[0] == knots[1] && knots[1] == knots[2] && knots[2] == knots[3] &&
        knots[knots.size() - 1] == knots[knots.size() - 2] && knots[knots.size() - 2] == knots[knots.size() - 3] &&
        knots[knots.size() - 3] == knots[knots.size() - 4]))
  {
    diagnostics.push_back({DiagnosticSeverity::Warning, record.line, layerOf(record), record.type,
                           "поддерживается только зажатый кубический SPLINE"});
    return;
  }

  std::vector<double> internal;
  for (std::size_t index = 4; index + 4 < knots.size(); ++index)
  {
    if (internal.empty() || internal.back() != knots[index])
      internal.push_back(knots[index]);
  }
  for (double value : internal)
  {
    while (static_cast<std::size_t>(std::count(knots.begin(), knots.end(), value)) < 3)
    {
      if (!insertKnotOnce(controls, knots, 3, value))
      {
        diagnostics.push_back({DiagnosticSeverity::Error, record.line, layerOf(record), record.type,
                               "не удалось разделить SPLINE на кубические сегменты"});
        return;
      }
    }
  }
  if ((controls.size() - 1) % 3 != 0)
  {
    diagnostics.push_back(
      {DiagnosticSeverity::Error, record.line, layerOf(record), record.type, "SPLINE не удалось представить кубическими Bézier"});
    return;
  }
  for (std::size_t index = 0; index + 3 < controls.size(); index += 3)
  {
    Segment segment;
    segment.kind = SegmentKind::CubicBezier;
    segment.start = controls[index];
    segment.control1 = controls[index + 1];
    segment.control2 = controls[index + 2];
    segment.end = controls[index + 3];
    segment.layer = layerOf(record);
    segment.sourceOrder = order * 100000 + index / 3;
    segment.line = record.line;
    segments.push_back(std::move(segment));
  }
}

/// Переворачивает сегмент вместе с направлением дуги и управляющими точками.
Segment reversed(Segment segment)
{
  std::swap(segment.start, segment.end);
  if (segment.kind == SegmentKind::Arc)
    segment.clockwise = !segment.clockwise;
  else if (segment.kind == SegmentKind::CubicBezier)
    std::swap(segment.control1, segment.control2);
  return segment;
}

/// Переворачивает целую цепочку без изменения её геометрии.
void reverseChain(Chain & chain)
{
  std::reverse(chain.segments.begin(), chain.segments.end());
  for (Segment & segment : chain.segments)
    segment = reversed(std::move(segment));
}

/// Собирает стабильные цепочки по точным совпадениям концов сегментов одного слоя.
std::vector<Chain> assembleChains(std::vector<Segment> segments, std::vector<Diagnostic> & diagnostics)
{
  std::stable_sort(segments.begin(), segments.end(), [](const Segment & lhs, const Segment & rhs)
                   { return std::tie(lhs.layer, lhs.sourceOrder) < std::tie(rhs.layer, rhs.sourceOrder); });
  std::vector<bool> used(segments.size(), false);
  std::vector<Chain> chains;
  for (std::size_t seed = 0; seed < segments.size(); ++seed)
  {
    if (used[seed])
      continue;
    Chain chain;
    chain.layer = segments[seed].layer;
    chain.segments.push_back(segments[seed]);
    used[seed] = true;
    bool extended = true;
    while (extended)
    {
      extended = false;
      const Point end = chain.segments.back().end;
      std::vector<std::pair<std::size_t, bool>> matches;
      for (std::size_t index = 0; index < segments.size(); ++index)
      {
        if (used[index] || segments[index].layer != chain.layer)
          continue;
        if (samePoint(segments[index].start, end))
          matches.emplace_back(index, false);
        else if (samePoint(segments[index].end, end))
          matches.emplace_back(index, true);
      }
      if (matches.size() == 1)
      {
        const auto [index, reverse] = matches.front();
        chain.segments.push_back(reverse ? reversed(segments[index]) : segments[index]);
        used[index] = true;
        extended = true;
      }
      else if (matches.size() > 1)
      {
        diagnostics.push_back({DiagnosticSeverity::Warning,
                               chain.segments.back().line,
                               chain.layer,
                               {},
                               "в точке соединения обнаружено ветвление; выбор продолжения оставлен пользователю"});
      }
    }
    extended = true;
    while (extended)
    {
      extended = false;
      const Point start = chain.segments.front().start;
      std::vector<std::pair<std::size_t, bool>> matches;
      for (std::size_t index = 0; index < segments.size(); ++index)
      {
        if (used[index] || segments[index].layer != chain.layer)
          continue;
        if (samePoint(segments[index].end, start))
          matches.emplace_back(index, false);
        else if (samePoint(segments[index].start, start))
          matches.emplace_back(index, true);
      }
      if (matches.size() == 1)
      {
        const auto [index, reverse] = matches.front();
        chain.segments.insert(chain.segments.begin(), reverse ? reversed(segments[index]) : segments[index]);
        used[index] = true;
        extended = true;
      }
      else if (matches.size() > 1)
      {
        diagnostics.push_back({DiagnosticSeverity::Warning,
                               chain.segments.front().line,
                               chain.layer,
                               {},
                               "в точке соединения обнаружено ветвление; выбор продолжения оставлен пользователю"});
      }
    }
    chain.closed = samePoint(chain.segments.front().start, chain.segments.back().end);
    chains.push_back(std::move(chain));
  }
  for (std::size_t index = 0; index < chains.size(); ++index)
    chains[index].id = index + 1;
  return chains;
}

/// Строит только однозначные предложения соединения открытых концов.
std::vector<JoinProposal> proposeJoins(const std::vector<Chain> & chains, double tolerance)
{
  struct End
  {
    std::size_t chain = 0;
    bool atStart = false;
    Point point;
  };
  std::vector<End> ends;
  for (std::size_t index = 0; index < chains.size(); ++index)
  {
    if (!chains[index].closed)
    {
      ends.push_back({index, true, chains[index].segments.front().start});
      ends.push_back({index, false, chains[index].segments.back().end});
    }
  }
  std::vector<std::vector<std::size_t>> candidates(ends.size());
  for (std::size_t first = 0; first < ends.size(); ++first)
  {
    for (std::size_t second = first + 1; second < ends.size(); ++second)
    {
      if (chains[ends[first].chain].layer != chains[ends[second].chain].layer)
        continue;
      const double gap = distance(ends[first].point, ends[second].point);
      if (gap > 0.0 && gap <= tolerance)
      {
        candidates[first].push_back(second);
        candidates[second].push_back(first);
      }
    }
  }
  std::vector<JoinProposal> proposals;
  for (std::size_t first = 0; first < ends.size(); ++first)
  {
    if (candidates[first].size() != 1)
      continue;
    const std::size_t second = candidates[first].front();
    if (second <= first || candidates[second].size() != 1 || candidates[second].front() != first)
      continue;
    proposals.push_back({0, chains[ends[first].chain].id, ends[first].atStart, chains[ends[second].chain].id,
                         ends[second].atStart, distance(ends[first].point, ends[second].point)});
  }
  std::stable_sort(proposals.begin(), proposals.end(),
                   [](const JoinProposal & lhs, const JoinProposal & rhs)
                   {
                     return std::tie(lhs.distanceMm, lhs.firstPath, lhs.secondPath) <
                            std::tie(rhs.distanceMm, rhs.firstPath, rhs.secondPath);
                   });
  for (std::size_t index = 0; index < proposals.size(); ++index)
    proposals[index].id = index + 1;
  return proposals;
}

/// Преобразует поддерживаемые записи в сегменты и сообщает все пропущенные сущности.
Geometry buildGeometry(const ParsedDxf & parsed, LengthUnit unit, double joinTolerance)
{
  Geometry result;
  if (!parsed.success || unit == LengthUnit::Unknown || !std::isfinite(joinTolerance) || joinTolerance < 0.0 ||
      joinTolerance > 1.0)
  {
    result.diagnostics.push_back(
      {DiagnosticSeverity::Error,
       0,
       {},
       {},
       unit == LengthUnit::Unknown ? "не выбрана единица длины DXF" : "допуск соединения должен лежать в диапазоне 0–1 мм"});
    return result;
  }
  const double factor = unitFactor(unit);
  std::vector<Segment> segments;
  std::map<std::string, LayerSummary> layers;
  for (std::size_t index = 0; index < parsed.entities.size(); ++index)
  {
    const Record & record = parsed.entities[index];
    if (record.type == "VERTEX" || record.type == "SEQEND")
      continue;
    LayerSummary & layer = layers[layerOf(record)];
    layer.name = layerOf(record);
    const bool supported = record.type == "LINE" || record.type == "LWPOLYLINE" || record.type == "POLYLINE" ||
                           record.type == "ARC" || record.type == "CIRCLE" || record.type == "SPLINE";
    if (!supported)
    {
      ++layer.unsupportedEntities;
      result.diagnostics.push_back({DiagnosticSeverity::Warning, record.line, layer.name, record.type,
                                    "сущность DXF не поддерживается и не была интерпретирована"});
      continue;
    }
    ++layer.supportedEntities;
    if (record.type == "LINE")
      convertLine(record, factor, index, segments, result.diagnostics);
    else if (record.type == "LWPOLYLINE")
      convertLightPolyline(record, factor, index, segments, result.diagnostics);
    else if (record.type == "POLYLINE")
      index = convertPolyline(parsed.entities, index, factor, segments, result.diagnostics);
    else if (record.type == "ARC")
      convertArc(record, factor, index, segments, result.diagnostics);
    else if (record.type == "CIRCLE")
      convertCircle(record, factor, index, segments, result.diagnostics);
    else if (record.type == "SPLINE")
      convertSpline(record, factor, index, segments, result.diagnostics);
  }
  if (segments.empty())
  {
    result.diagnostics.push_back({DiagnosticSeverity::Error, 0, {}, {}, "DXF не содержит поддерживаемой плоской геометрии"});
    return result;
  }
  result.chains = assembleChains(std::move(segments), result.diagnostics);
  result.proposals = proposeJoins(result.chains, joinTolerance);
  result.success = true;
  return result;
}

/// Вычисляет габариты цепочки по всем определяющим точкам сегментов.
std::array<double, 4> bounds(const Chain & chain)
{
  double minX = std::numeric_limits<double>::infinity();
  double minY = std::numeric_limits<double>::infinity();
  double maxX = -std::numeric_limits<double>::infinity();
  double maxY = -std::numeric_limits<double>::infinity();
  const auto inspect = [&](Point point)
  {
    minX = std::min(minX, point.x);
    minY = std::min(minY, point.y);
    maxX = std::max(maxX, point.x);
    maxY = std::max(maxY, point.y);
  };
  for (const Segment & segment : chain.segments)
  {
    inspect(segment.start);
    inspect(segment.end);
    if (segment.kind == SegmentKind::Arc)
      inspect(segment.center);
    else if (segment.kind == SegmentKind::CubicBezier)
    {
      inspect(segment.control1);
      inspect(segment.control2);
    }
  }
  return {minX, minY, maxX, maxY};
}

/// Проверяет строгое вложение габаритов как безопасное первоначальное предложение отверстия.
bool boxInside(const std::array<double, 4> & inner, const std::array<double, 4> & outer) noexcept
{
  return inner[0] > outer[0] && inner[1] > outer[1] && inner[2] < outer[2] && inner[3] < outer[3];
}

/// Заполняет нейтральное описание цепочек и предлагает ближайший внешний контейнер.
std::vector<PathSummary> summarizePaths(const std::vector<Chain> & chains)
{
  std::vector<PathSummary> summaries;
  summaries.reserve(chains.size());
  std::vector<std::array<double, 4>> boxes;
  for (const Chain & chain : chains)
  {
    const auto box = bounds(chain);
    boxes.push_back(box);
    summaries.push_back({chain.id, chain.layer, chain.closed, box[0], box[1], box[2], box[3], chain.segments.size(), 0});
  }
  for (std::size_t inner = 0; inner < chains.size(); ++inner)
  {
    if (!chains[inner].closed)
      continue;
    double bestArea = std::numeric_limits<double>::infinity();
    for (std::size_t outer = 0; outer < chains.size(); ++outer)
    {
      if (inner == outer || !chains[outer].closed || !boxInside(boxes[inner], boxes[outer]))
        continue;
      const double area = (boxes[outer][2] - boxes[outer][0]) * (boxes[outer][3] - boxes[outer][1]);
      if (area < bestArea)
      {
        bestArea = area;
        summaries[inner].suggestedOuterPath = chains[outer].id;
      }
    }
  }
  return summaries;
}

/// Применяет подтверждённые попарно однозначные соединения к рабочим цепочкам.
void applyAcceptedJoins(std::vector<Chain> & chains, const std::vector<JoinProposal> & proposals,
                        const std::vector<std::uint64_t> & accepted)
{
  const std::set<std::uint64_t> selected(accepted.begin(), accepted.end());
  for (const JoinProposal & proposal : proposals)
  {
    if (!selected.contains(proposal.id))
      continue;
    auto first = std::find_if(chains.begin(), chains.end(), [&](const Chain & chain) { return chain.id == proposal.firstPath; });
    auto second =
      std::find_if(chains.begin(), chains.end(), [&](const Chain & chain) { return chain.id == proposal.secondPath; });
    if (first == chains.end() || second == chains.end())
      continue;
    if (first == second)
    {
      const Point midpoint{(first->segments.front().start.x + first->segments.back().end.x) * 0.5,
                           (first->segments.front().start.y + first->segments.back().end.y) * 0.5};
      first->segments.front().start = midpoint;
      first->segments.back().end = midpoint;
      first->closed = true;
      continue;
    }
    if (proposal.firstAtStart)
      reverseChain(*first);
    if (!proposal.secondAtStart)
      reverseChain(*second);
    const Point midpoint{(first->segments.back().end.x + second->segments.front().start.x) * 0.5,
                         (first->segments.back().end.y + second->segments.front().start.y) * 0.5};
    first->segments.back().end = midpoint;
    second->segments.front().start = midpoint;
    first->segments.insert(first->segments.end(), second->segments.begin(), second->segments.end());
    first->closed = samePoint(first->segments.front().start, first->segments.back().end);
    chains.erase(second);
  }
}

/// Переносит внутренний сегмент в модель редактора и выдаёт идентификаторы служебных точек.
editor::EditableSegment makeEditableSegment(const Segment & source, editor::EditablePolygonDocument & document, double offsetX,
                                            double offsetY)
{
  editor::EditableSegment result;
  result.id = document.allocateEntityId();
  result.kind = source.kind == SegmentKind::Line ? editor::EditableSegmentKind::Line
              : source.kind == SegmentKind::Arc  ? editor::EditableSegmentKind::Arc
                                                 : editor::EditableSegmentKind::CubicBezier;
  if (source.kind == SegmentKind::Arc)
  {
    result.center = {document.allocateEntityId(), source.center.x - offsetX, source.center.y - offsetY};
    result.clockwise = source.clockwise;
  }
  else if (source.kind == SegmentKind::CubicBezier)
  {
    result.control1 = {document.allocateEntityId(), source.control1.x - offsetX, source.control1.y - offsetY};
    result.control2 = {document.allocateEntityId(), source.control2.x - offsetX, source.control2.y - offsetY};
  }
  return result;
}

/// Создаёт редактируемый путь с локальными координатами относительно внешней детали.
editor::EditablePath makeEditablePath(const Chain & source, editor::EditablePolygonDocument & document, double offsetX,
                                      double offsetY)
{
  editor::EditablePath result;
  result.id = document.allocateEntityId();
  result.closed = source.closed;
  if (source.segments.empty())
    return result;
  result.vertices.push_back(
    {document.allocateEntityId(), source.segments.front().start.x - offsetX, source.segments.front().start.y - offsetY});
  for (const Segment & segment : source.segments)
  {
    result.segments.push_back(makeEditableSegment(segment, document, offsetX, offsetY));
    if (!source.closed || !samePoint(segment.end, source.segments.front().start))
      result.vertices.push_back({document.allocateEntityId(), segment.end.x - offsetX, segment.end.y - offsetY});
  }
  return result;
}

/// Находит цепочку по устойчивому идентификатору текущего анализа.
const Chain * findChain(const std::vector<Chain> & chains, std::uint64_t id) noexcept
{
  const auto found = std::find_if(chains.begin(), chains.end(), [id](const Chain & chain) { return chain.id == id; });
  return found == chains.end() ? nullptr : &*found;
}

/// Формирует точный независимый от локали литерал координаты для сравнения геометрии.
std::string numberKey(double value)
{
  if (value == 0.0)
    value = 0.0;
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::hexfloat << value;
  return stream.str();
}

/// Кодирует один сегмент относительно общего начала детали.
std::string segmentKey(const Segment & segment, double offsetX, double offsetY)
{
  std::string result = std::to_string(static_cast<int>(segment.kind));
  const auto append = [&](Point point)
  {
    result += ':' + numberKey(point.x - offsetX) + ',' + numberKey(point.y - offsetY);
  };
  append(segment.start);
  append(segment.end);
  if (segment.kind == SegmentKind::Arc)
  {
    append(segment.center);
    result += segment.clockwise ? ":cw" : ":ccw";
  }
  else if (segment.kind == SegmentKind::CubicBezier)
  {
    append(segment.control1);
    append(segment.control2);
  }
  return result;
}

/// Выбирает лексикографически минимальный циклический обход кольца.
std::string canonicalCycle(const std::vector<std::string> & tokens)
{
  if (tokens.empty())
    return {};
  std::string best;
  for (std::size_t shift = 0; shift < tokens.size(); ++shift)
  {
    std::string candidate;
    for (std::size_t index = 0; index < tokens.size(); ++index)
      candidate += '|' + tokens[(index + shift) % tokens.size()];
    if (best.empty() || candidate < best)
      best = std::move(candidate);
  }
  return best;
}

/// Соединяет сегменты открытой цепочки без циклического сдвига.
std::string linearKey(const std::vector<std::string> & tokens)
{
  std::string result;
  for (const std::string & token : tokens)
    result += '|' + token;
  return result;
}

/// Формирует инвариантную к переносу, началу и направлению подпись цепочки.
std::string chainSignature(const Chain & chain, double offsetX, double offsetY)
{
  std::vector<std::string> forward;
  forward.reserve(chain.segments.size());
  for (const Segment & segment : chain.segments)
    forward.push_back(segmentKey(segment, offsetX, offsetY));
  Chain backward = chain;
  reverseChain(backward);
  std::vector<std::string> reverse;
  reverse.reserve(backward.segments.size());
  for (const Segment & segment : backward.segments)
    reverse.push_back(segmentKey(segment, offsetX, offsetY));
  const std::string forwardKey = chain.closed ? canonicalCycle(forward) : linearKey(forward);
  const std::string reverseKey = chain.closed ? canonicalCycle(reverse) : linearKey(reverse);
  return std::min(forwardKey, reverseKey);
}

/// Формирует подпись детали вместе с неупорядоченным набором отверстий.
std::string partSignature(const std::vector<Chain> & chains, const PartAssignment & assignment)
{
  const Chain * outer = findChain(chains, assignment.outerPath);
  if (!outer)
    return {};
  const auto box = bounds(*outer);
  std::string result = chainSignature(*outer, box[0], box[1]);
  std::vector<std::string> holes;
  for (std::uint64_t holeId : assignment.holes)
  {
    if (const Chain * hole = findChain(chains, holeId))
      holes.push_back(chainSignature(*hole, box[0], box[1]));
  }
  std::sort(holes.begin(), holes.end());
  for (const std::string & hole : holes)
    result += "#" + hole;
  return result;
}

/// Проверяет, выбран ли слой; пустой список означает выбор всех слоёв.
bool layerSelected(const std::vector<std::string> & selected, const std::string & layer)
{
  return selected.empty() || std::find(selected.begin(), selected.end(), layer) != selected.end();
}
} // namespace

/// Сопоставляет пользовательский машинный литерал с поддерживаемой единицей.
std::optional<LengthUnit> parseLengthUnit(std::string_view value) noexcept
{
  constexpr std::array values{std::pair{"mm", LengthUnit::Millimeter}, std::pair{"cm", LengthUnit::Centimeter},
                              std::pair{"m", LengthUnit::Meter},       std::pair{"km", LengthUnit::Kilometer},
                              std::pair{"in", LengthUnit::Inch},       std::pair{"ft", LengthUnit::Foot},
                              std::pair{"yd", LengthUnit::Yard},       std::pair{"um", LengthUnit::Micrometer}};
  const auto found = std::find_if(values.begin(), values.end(), [value](const auto & item) { return item.first == value; });
  return found == values.end() ? std::nullopt : std::optional{found->second};
}

/// Возвращает устойчивый литерал единицы без локализованного представления.
std::string_view lengthUnitName(LengthUnit unit) noexcept
{
  switch (unit)
  {
    case LengthUnit::Millimeter:
      return "mm";
    case LengthUnit::Centimeter:
      return "cm";
    case LengthUnit::Meter:
      return "m";
    case LengthUnit::Kilometer:
      return "km";
    case LengthUnit::Inch:
      return "in";
    case LengthUnit::Foot:
      return "ft";
    case LengthUnit::Yard:
      return "yd";
    case LengthUnit::Micrometer:
      return "um";
    case LengthUnit::Unknown:
      return "unknown";
  }
  return "unknown";
}

/// Повторно разбирает файл в выбранных единицах и выдаёт только нейтральное описание.
Inspection inspectAsciiDxf(std::string_view contents, std::optional<LengthUnit> unitOverride, double joinToleranceMm,
                           const std::vector<std::uint64_t> & acceptedJoinProposals)
{
  Inspection result;
  ParsedDxf parsed = parseDxf(contents);
  result.acadVersion = parsed.acadVersion;
  result.detectedUnit = parsed.unit;
  result.unitSelectionRequired = parsed.unit == LengthUnit::Unknown && !unitOverride;
  result.diagnostics = parsed.diagnostics;
  if (!parsed.success)
    return result;
  const LengthUnit unit = unitOverride.value_or(parsed.unit);
  if (unit == LengthUnit::Unknown)
  {
    result.diagnostics.push_back(
      {DiagnosticSeverity::Error, 0, {}, {}, "DXF не задаёт поддерживаемую единицу; выберите её явно"});
    result.parsed = true;
    return result;
  }
  Geometry geometry = buildGeometry(parsed, unit, joinToleranceMm);
  result.parsed = geometry.success;
  applyAcceptedJoins(geometry.chains, geometry.proposals, acceptedJoinProposals);
  result.paths = summarizePaths(geometry.chains);
  result.joinProposals = std::move(geometry.proposals);
  result.diagnostics.insert(result.diagnostics.end(), geometry.diagnostics.begin(), geometry.diagnostics.end());
  std::map<std::string, LayerSummary> layers;
  for (const Record & record : parsed.entities)
  {
    if (record.type == "VERTEX" || record.type == "SEQEND")
      continue;
    LayerSummary & layer = layers[layerOf(record)];
    layer.name = layerOf(record);
    if (record.type == "LINE" || record.type == "LWPOLYLINE" || record.type == "POLYLINE" || record.type == "ARC" ||
        record.type == "CIRCLE" || record.type == "SPLINE")
      ++layer.supportedEntities;
    else
      ++layer.unsupportedEntities;
  }
  for (auto & [name, layer] : layers)
    result.layers.push_back(std::move(layer));
  return result;
}

/// Строит документ только из явно назначенных цепочек выбранных слоёв.
ImportResult importAsciiDxf(std::string_view contents, const ImportOptions & options)
{
  ImportResult result;
  ParsedDxf parsed = parseDxf(contents);
  result.diagnostics = parsed.diagnostics;
  if (!parsed.success)
    return result;
  const LengthUnit unit = options.unitOverride.value_or(parsed.unit);
  Geometry geometry = buildGeometry(parsed, unit, options.joinToleranceMm);
  result.diagnostics.insert(result.diagnostics.end(), geometry.diagnostics.begin(), geometry.diagnostics.end());
  if (!geometry.success)
    return result;
  applyAcceptedJoins(geometry.chains, geometry.proposals, options.acceptedJoinProposals);

  bool unsupportedSelected = false;
  for (const Diagnostic & diagnostic : result.diagnostics)
  {
    if (diagnostic.message.find("не поддерживается") != std::string::npos &&
        layerSelected(options.selectedLayers, diagnostic.layer))
      unsupportedSelected = true;
  }
  if (unsupportedSelected && !options.ignoreUnsupportedOnSelectedLayers)
  {
    result.diagnostics.push_back(
      {DiagnosticSeverity::Error, 0, {}, {}, "на выбранных слоях есть неподдерживаемые сущности без подтверждения пропуска"});
    return result;
  }

  editor::EditablePolygonDocument document;
  document.problemId = options.problemId;
  document.sheet.width = options.sheetWidth;
  document.sheet.height = options.sheetHeight;
  document.manufacturing = {options.sheetMargin, options.partSpacing, options.kerf, options.curveTolerance};

  std::vector<PartAssignment> assignments = options.parts;
  if (assignments.empty())
  {
    Inspection inspection = inspectAsciiDxf(contents, unit, options.joinToleranceMm, options.acceptedJoinProposals);
    assignments = proposeParts(inspection);
  }
  if (options.groupDuplicateParts)
  {
    std::map<std::string, std::size_t> groups;
    std::vector<PartAssignment> grouped;
    for (const PartAssignment & assignment : assignments)
    {
      const std::string signature = partSignature(geometry.chains, assignment);
      const auto found = groups.find(signature);
      if (signature.empty() || found == groups.end())
      {
        groups.emplace(signature, grouped.size());
        grouped.push_back(assignment);
      }
      else
      {
        const std::uint64_t quantity = static_cast<std::uint64_t>(grouped[found->second].quantity) + assignment.quantity;
        grouped[found->second].quantity = quantity > std::numeric_limits<std::uint32_t>::max()
                                          ? std::numeric_limits<std::uint32_t>::max()
                                          : static_cast<std::uint32_t>(quantity);
      }
    }
    assignments = std::move(grouped);
  }
  for (const PartAssignment & assignment : assignments)
  {
    const Chain * outer = findChain(geometry.chains, assignment.outerPath);
    if (!outer || !layerSelected(options.selectedLayers, outer->layer))
    {
      result.diagnostics.push_back(
        {DiagnosticSeverity::Error, 0, {}, {}, "назначенное внешнее кольцо отсутствует на выбранных слоях"});
      continue;
    }
    const auto box = bounds(*outer);
    editor::EditablePart part;
    part.id = document.allocateEntityId();
    part.partId = assignment.partId;
    part.quantity = assignment.quantity;
    part.allowedRotations = assignment.allowedRotations;
    part.outer = makeEditablePath(*outer, document, box[0], box[1]);
    for (std::uint64_t holeId : assignment.holes)
    {
      const Chain * hole = findChain(geometry.chains, holeId);
      if (!hole || !layerSelected(options.selectedLayers, hole->layer))
      {
        result.diagnostics.push_back(
          {DiagnosticSeverity::Error, 0, {}, {}, "назначенное отверстие отсутствует на выбранных слоях"});
        continue;
      }
      part.holes.push_back(makeEditablePath(*hole, document, box[0], box[1]));
    }
    document.parts.push_back(std::move(part));
  }
  result.documentDiagnostics = editor::validateEditableDocument(document);
  result.document = std::move(document);
  result.built = !std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                              [](const Diagnostic & diagnostic) { return diagnostic.severity == DiagnosticSeverity::Error; });
  return result;
}

/// Назначает непосредственные вложенные цепочки отверстиями, а остальные — отдельными деталями.
std::vector<PartAssignment> proposeParts(const Inspection & inspection)
{
  std::vector<PartAssignment> result;
  std::size_t partIndex = 1;
  for (const PathSummary & path : inspection.paths)
  {
    if (!path.closed || path.suggestedOuterPath != 0)
      continue;
    PartAssignment assignment;
    assignment.outerPath = path.id;
    assignment.partId = "part-" + std::to_string(partIndex++);
    for (const PathSummary & candidate : inspection.paths)
    {
      if (candidate.suggestedOuterPath == path.id)
        assignment.holes.push_back(candidate.id);
    }
    result.push_back(std::move(assignment));
  }
  for (const PathSummary & path : inspection.paths)
  {
    if (path.closed)
      continue;
    PartAssignment assignment;
    assignment.outerPath = path.id;
    assignment.partId = "part-" + std::to_string(partIndex++);
    result.push_back(std::move(assignment));
  }
  return result;
}
} // namespace aipackaging::dxf
