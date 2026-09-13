#ifndef AIPACKAGING_TESTS_POLYGON_FIXTURE_H
#define AIPACKAGING_TESTS_POLYGON_FIXTURE_H

#include <utility>

#include <aipackaging/nesting/polygon_types.h>

namespace aipackaging::tests
{
using namespace solver;

/// Создаёт замкнутый прямоугольный путь для независимых модульных тестов.
inline PolygonPath rectangle(double width, double height, double x = 0.0, double y = 0.0)
{
  PolygonPath result;
  result.start = {x, y};
  result.segments = {{PolygonSegmentKind::Line, {x + width, y}},
                     {PolygonSegmentKind::Line, {x + width, y + height}},
                     {PolygonSegmentKind::Line, {x, y + height}},
                     {PolygonSegmentKind::Line, {x, y}}};
  return result;
}

/// Создаёт минимальную валидную полигональную задачу с двумя экземплярами.
inline PolygonProblem problem()
{
  PolygonProblem result;
  result.problemId = "polygon-test";
  result.sheet = {100.0, 60.0, "mm"};
  result.manufacturing = {2.0, 1.0, 0.2, 0.05};
  PolygonPart part;
  part.id = "rectangle";
  part.quantity = 2;
  part.outer = rectangle(30.0, 20.0);
  part.allowedRotations = {0, 90, 180, 270};
  result.parts.push_back(std::move(part));
  return result;
}
} // namespace aipackaging::tests

#endif
