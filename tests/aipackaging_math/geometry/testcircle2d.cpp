#include <circle2d.h>
#include <gtest/gtest.h>
#include <mathutils.h>
#include <matrix3.h>

// Базовые случаи

// Тест метода получения точки на окружности
TEST(circle2D, GetPointBase)
{
  Point2D center{0.0, 0.0};
  double radius = 5.0;
  Circle2D circle(center, radius);

  struct TestCase
  {
    double angle;
    double expectedX;
    double expectedY;
  };

  TestCase testCases[] = {
    {MathConstants::PI / 2, 0.0, radius},      // Верхняя точка
    {3 * MathConstants::PI / 2, 0.0, -radius}, // Нижняя точка
    {MathConstants::PI, -radius, 0.0},         // Левая точка
    {0.0, radius, 0.0}                         // Правая точка
  };

  // Проходим по всем тестовым случаям
  for (const auto & testCase : testCases)
  {
    Point2D point = circle.getPoint(testCase.angle);
    EXPECT_NEAR(testCase.expectedX, point.x, MathConstants::TOLERANCE_DOUBLE);
    EXPECT_NEAR(testCase.expectedY, point.y, MathConstants::TOLERANCE_DOUBLE);
  }
}


// Базовый тест Move
TEST(circle2D, MoveBase)
{
  Point2D center{0, 0};
  double radius = 3;

  Circle2D circle{center, radius};

  Vector2D offset{1, 1};

  circle.move(offset);

  Point2D expected{1, 1};
  double centerX = (circle.getPoint(0).x + circle.getPoint(MathConstants::PI).x) / 2;
  double centerY = (circle.getPoint(0).y + circle.getPoint(MathConstants::PI).y) / 2;
  Point2D actual{centerX, centerY};

  EXPECT_EQ(expected, actual);
}


//
TEST(circle2D, RotateBase)
{
  Point2D center{0, 0};
  double radius = 3;

  Circle2D circle{center, radius};

  double radian = MathConstants::PI;

  circle.rotate(radian);

  Point2D expected{0, 0};
  double centerX = (circle.getPoint(0).x + circle.getPoint(MathConstants::PI).x) / 2;
  double centerY = (circle.getPoint(0).y + circle.getPoint(MathConstants::PI).y) / 2;
  Point2D actual{centerX, centerY};

  EXPECT_EQ(expected, actual);
}


// Тест метода трансформации окружности (Перемещение)
TEST(circle2D, TransformMove)
{
  Point2D center{0.0, 0.0};
  double radius = 5.0;
  Circle2D circle(center, radius);

  Vector2D offset{2.0, 3.0};

  Point2D point = circle.getPoint(0);
  Point2D expectedPoint0(point + offset);

  circle.transform(Matrix3::translate(offset));

  Point2D actualPoint0 = circle.getPoint(0);

  EXPECT_EQ(expectedPoint0, actualPoint0);
}


// Трансформация скалирования
TEST(circle2D, TransformScale)
{
  Point2D center{0.0, 0.0};
  double radius = 5.0;
  Circle2D circle(center, radius);

  // Масштабирование в два раза
  Vector2D scale{2.0, 2.0};
  Matrix3 scaleMatrix = Matrix3::scale(scale.x, scale.y);

  // Точка на окружности до трансформации
  Point2D pointBefore = circle.getPoint(0.0); // Правая точка
  // Применяем трансформацию
  circle.transform(scaleMatrix);

  // Точка после трансформации
  Point2D actualPoint = circle.getPoint(0.0); // Правая точка

  Point2D expectedPoint{pointBefore.x * 2, pointBefore.y * 2};

  // Ожидаем, что точка на окружности удвоится по координатам
  EXPECT_EQ(expectedPoint, actualPoint);
}


// ----------------------------------------------------------------
// НЕОРДИНАРНЫЕ СЛУЧАИ

// Получение точки при нулевом радиусе
TEST(circle2D, GetPointZeroRadius)
{
  Point2D center{0.0, 0.0};
  double radius = 0.0;
  Circle2D circle(center, radius);

  double angles[] = {0.0, MathConstants::PI / 2, MathConstants::PI, 3 * MathConstants::PI / 2};

  for (double angle : angles)
  {
    Point2D expected = circle.getPoint(angle);
    EXPECT_NEAR(expected.x, 0.0, MathConstants::TOLERANCE_DOUBLE);
    EXPECT_NEAR(expected.y, 0.0, MathConstants::TOLERANCE_DOUBLE);
  }
}


// Получение точки при очень большом радиусе
TEST(circle2D, GetPointLargeRadius)
{
  Point2D center{0.0, 0.0};
  double radius = 1e6;
  Circle2D circle(center, radius);

  Point2D expected = circle.getPoint(0.0);
  EXPECT_NEAR(expected.x, radius, MathConstants::TOLERANCE_DOUBLE);
  EXPECT_NEAR(expected.y, 0.0, MathConstants::TOLERANCE_DOUBLE);
}


// Получение точки при очень маленьком радиусе
TEST(circle2D, GetPointSmallRadius)
{
  Point2D center{0.0, 0.0};
  double radius = 1e-6;
  Circle2D circle(center, radius);

  Point2D expected = circle.getPoint(0.0);
  EXPECT_NEAR(expected.x, radius, MathConstants::TOLERANCE_DOUBLE);
  EXPECT_NEAR(expected.y, 0.0, MathConstants::TOLERANCE_DOUBLE);
}
