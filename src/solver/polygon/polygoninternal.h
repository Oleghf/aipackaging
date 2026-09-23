#ifndef AIPACKAGING_POLYGON_INTERNAL_H
#define AIPACKAGING_POLYGON_INTERNAL_H

#include <cstdint>
#include <vector>

#include <aipackaging/nesting/polygon_environment.h>

namespace aipackaging::solver::internal
{
using Wide = long double;

inline constexpr long double MICRONS_PER_MM = 1000.0L;
inline constexpr std::int64_t MAX_SHEET_UM = 10'000'000;
inline constexpr std::size_t MAX_INSTANCES = 100;
inline constexpr std::size_t MAX_VERTICES = 2000;
inline constexpr std::size_t MAX_CANDIDATES = 250000;
inline constexpr int RASTER_SIZE = 128;

/// Возвращает ориентированное векторное произведение трёх микронных точек.
Wide cross(const PolygonPoint64 & a, const PolygonPoint64 & b, const PolygonPoint64 & c);
/// Классифицирует пересечение отрезков: отсутствие, касание или положительное пересечение.
int segmentIntersection(const PolygonPoint64 & a, const PolygonPoint64 & b, const PolygonPoint64 & c, const PolygonPoint64 & d);
/// Возвращает минимальный квадрат расстояния между границами двух колец.
Wide ringDistanceSquared(const PolygonRing64 & lhs, const PolygonRing64 & rhs);
/// Классифицирует точку относительно кольца как внешнюю, граничную или внутреннюю.
int pointInRing(const PolygonPoint64 & point, const PolygonRing64 & ring);
/// Сообщает о положительном перекрытии заполненных внешних колец.
bool interiorsOverlap(const PolygonRing64 & lhs, const PolygonRing64 & rhs);
/// Безопасно переносит кольцо на заданный микронный вектор и сообщает о переполнении.
bool translateRing(const PolygonRing64 & ring, std::int64_t x, std::int64_t y, PolygonRing64 & result);
} // namespace aipackaging::solver::internal

#endif
