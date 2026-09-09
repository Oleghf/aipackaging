#include <boundedcurve2d.h>
#include <gtest/gtest.h>
#include <line2d.h>
#include <mathutils.h>

// БАЗОВЫЕ СЛУЧАИ

// Проверка на корректную установку значений
TEST(boundedCurve2D, setMinMax)
{
  std::shared_ptr<ICurve2D> someCurve = std::make_shared<Line2D>(Point2D{0, 0}, Vector2D{3.0, 4.0});
  BoundedCurve2D boundedCurve(someCurve, 0.0, 10.0);

  boundedCurve.setMinT(0.5);
  boundedCurve.setMaxT(2.0);

  double actual1 = boundedCurve.getMinT();
  double expected1 = 0.5;

  double actual2 = boundedCurve.getMaxT();
  double expected2 = 2.0;

  EXPECT_NEAR(expected1, actual1, MathConstants::TOLERANCE_DOUBLE);
  EXPECT_NEAR(expected2, actual2, MathConstants::TOLERANCE_DOUBLE);
}


// ----------------------------------------------------
// НЕОРДИНАРНЫЕ СЛУЧАИ


// Тестирует то, что будет если значение maxT будет меньше чем minT
// и наоборот
TEST(boundedCurve2D, MaxTInMinT)
{
  Point2D point{0.0, 0.0};
  Vector2D direction{1.0, 3.0};

  std::shared_ptr<Line2D> testCurve = std::make_shared<Line2D>(point, direction);

  BoundedCurve2D boundCurve(testCurve, 15, -1);

  double expectedMin = -1;
  double actualMin = boundCurve.getMinT();

  double expectedMax = 15;
  double actualMax = boundCurve.getMaxT();

  EXPECT_NEAR(expectedMin, actualMin, MathConstants::TOLERANCE_DOUBLE);
  EXPECT_NEAR(expectedMax, actualMax, MathConstants::TOLERANCE_DOUBLE);
}


// Тест использования двух кривых на одной bounded curve
TEST(boundedCurve2D, TwoCurveOnOneBoundedCurve)
{
  Point2D point{0.0, 0.0};
  Vector2D direction{1.0, 3.0};

  std::shared_ptr<Line2D> testCurve = std::make_shared<Line2D>(point, direction);

  // Если не будет выброшено исключение то тест пройден
  ASSERT_NO_THROW(BoundedCurve2D bc1(testCurve, -5.0, 1.0); BoundedCurve2D bc2(testCurve, 5.0, 10.0););
}


// Тест вхождения одной ограниченной кривой в другую
TEST(boundedCurve2D, OneBoundedCurveContainsAnother)
{
  Point2D point{0.0, 0.0};
  Vector2D direction{1.0, 3.0};

  std::shared_ptr<Line2D> testCurve = std::make_shared<Line2D>(point, direction);

  ASSERT_NO_THROW(BoundedCurve2D bc1(testCurve, 6.0, 15.0); BoundedCurve2D bc2(testCurve, 5.0, 10.0););
}


// Тест использования отрицательных параметров в boundedCurve
TEST(boundedCurve2D, NegativeT)
{
  Point2D point{0.0, 0.0};
  Vector2D direction{1.0, 3.0};

  std::shared_ptr<Line2D> testCurve = std::make_shared<Line2D>(point, direction);

  ASSERT_NO_THROW(BoundedCurve2D bc(testCurve, -5, -7););
}
