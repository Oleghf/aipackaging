#include <cmath>

#include <circle2d.h>
#include <gtest/gtest.h>
#include <line2d.h>
#include <mathutils.h>
#include <point2d.h>


// БАЗОВЫЕ СЛУЧАИ

// Базовый случай сравнения двух double
TEST(doubleEQ, Base)
{
  double d1 = 0.00000006;
  double d2 = 0.00000005;

  bool expected = false;
  bool actual = doubleEQ(d1, d2);

  EXPECT_EQ(expected, actual);
}


// Базовый случай перевода из градусов в радианы
TEST(degreesToRadians, Base)
{
  double degrees = 90;

  double expected = MathConstants::PI / 2;
  double actual = degreesToRadians(degrees);

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


// Тестирует тривиальный случай, основанный на теореме Пифагора
TEST(distance, PointToPointBase)
{
  Point2D testP1{0, 0};
  Point2D testP2{3, 4};

  double actual = distance(testP1, testP2);
  double expected = 5.0;

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


// Тестирует тривиальный случай
TEST(distance, PointToLineBase)
{
  Point2D point{1, 0};
  Point2D testLP{0, 0};
  Line2D line(testLP, Vector2D(Point2D{2, 2} - testLP));

  double actual = distance(point, line);
  double expected = std::sqrt(2) / 2;

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


//
TEST(distance, PointToCircleBase)
{
  Point2D point{0, 0};
  double radius = 3;

  Circle2D circle(point, radius);

  double expected = 3;
  double actual = distance(point, circle);

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


// -------------------------------------------
// НЕОРДИНАРНЫЕ СЛУЧАИ


// Сравнение очень маленьких double
TEST(doubleEQ, EQ_VerySmall)
{
  double epsilon = 1e-11;
  double d1 = 0.00000000002;
  double d2 = 0.00000000003;

  bool expected = false;
  bool actual = doubleEQ(d1, d2, epsilon);

  EXPECT_EQ(expected, actual);
}


// Сравнение очень больших double
TEST(doubleEQ, EQ_VeryBig)
{
  double d1 = 50000000.20000005;
  double d2 = 50000000.20000006;

  bool expected = false;
  bool actual = doubleEQ(d1, d2);

  EXPECT_EQ(expected, actual);
}


// Преобразование отрицательных значений
TEST(degreesToRadians, negativeDegrees)
{
  double degrees = -90.0;
  double expected = -MathConstants::PI / 2.0;
  double actual = degreesToRadians(degrees);

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


// Преобразование дробных значений
TEST(degreesToRadians, decimalDegrees)
{
  double degrees = 45.5;
  double expected = degrees * (MathConstants::PI / 180.0);
  double actual = degreesToRadians(degrees);

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


// Угол между одинаковыми векторами
TEST(calculateAngle, sameVectors)
{
  Vector2D v1{1, 0};
  Vector2D v2{1, 0};

  double expected = 0.0;
  double actual = calculateAngle(v1, v2);

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


// Угол между перпендикулярными векторами
TEST(calculateAngle, perpendicularVectors)
{
  Vector2D v1{1, 0};
  Vector2D v2{0, 1};

  double expected = MathConstants::PI / 2;
  double actual = calculateAngle(v1, v2);

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


// Угол между противоположными векторами
TEST(calculateAngle, oppositeVectors)
{
  Vector2D v1{1, 0};
  Vector2D v2{-1, 0};

  double expected = MathConstants::PI;
  double actual = calculateAngle(v1, v2);

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


// Тестирует нахождение расстояния между одной и той же точкой
TEST(distance, PointToPointSamePoint)
{
  Point2D testP1{1, 1};

  double actual = distance(testP1, testP1);
  double expected = 0;

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


// Тестирует расстояние между точками с нулевыми координатами
TEST(distance, PointToPointZeroCoordinates)
{
  Point2D testP1{0, 0};

  double actual = distance(testP1, testP1);
  double expected = 0;

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


// Тестирует нахождение расстояние между линией и точкой на этой линии
TEST(distance, PointToLinePointOnTheLine)
{
  Point2D testP{0, 0};
  Vector2D testVec{1, 1};

  Line2D testLine(testP, testVec);

  double actual = distance(testP, testLine);
  double expected = 0;

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


// Тестирует, нахождения расстояния между точкой внутри окружности и окружностью
TEST(distance, PointToCirclePointAtCircle)
{
  Point2D p1 = {3, 0};
  Circle2D circle = {{0, 0}, 5};

  double expected = 2.0;
  double actual = distance(p1, circle);

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


// Тестирует, нахождение расстояния между точкой вне окружности и окружностью
TEST(distance, PointToCirclePointOnCircle)
{
  Point2D p1{8, 0};
  Circle2D circle = {{0, 0}, 5};

  double expected = 3.0;
  double actual = distance(p1, circle);

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


// Тестирует, нахождение расстояния между точкой в центре окружности и окружностью
TEST(distance, PointToCirclePointAtCenter)
{
  Point2D p1{0, 0};
  Circle2D circle{p1, 5};

  double expected = 5.0;
  double actual = distance(p1, circle);

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}
