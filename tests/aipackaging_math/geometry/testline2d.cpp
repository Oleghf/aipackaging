#include <cmath>

#include <gtest/gtest.h>
#include <line2d.h>
#include <mathutils.h>
#include <matrix3.h>
#include <point2d.h>
#include <vector2d.h>

// БАЗОВЫЕ СЛУЧАИ

// Тест метода возвращающего точку по параметру
TEST(line2D, GetPointBase)
{
  Point2D startPoint{1.0, 3.0};
  Vector2D direction{3.0, 4.0};

  Line2D line(startPoint, direction);

  Point2D expected = startPoint;
  Point2D actual = line.getPoint(0);

  EXPECT_EQ(expected, actual);
}


// Тест метода сдвига на вектору
TEST(line2D, MoveBase)
{
  Point2D point{0, 0};
  Vector2D direction{0, 1};

  Line2D line(point, direction);
  Vector2D offset{1, 1};

  line.move(offset);

  Point2D actualPoint = line.getPoint(0);
  Vector2D actualDir = line.getPoint(1) - line.getPoint(0);

  Point2D expectedPoint{1, 1};
  Vector2D expectedDir{0, 1};

  EXPECT_EQ(expectedPoint, actualPoint);
  EXPECT_EQ(expectedDir, actualDir);
}


// Тест метода поворота
TEST(line2D, RotateBase)
{
  // Исходные данные
  Point2D expectedPoint{1.0, 0.0};
  Vector2D expectedDirection{0.0, 1.0};
  Vector2D testDirection{1.0, 0.0};

  // Создаем линию
  Line2D line(expectedPoint, testDirection);

  // Применяем поворот на 90 градусов
  line.rotate(MathConstants::PI / 2);

  // Проверяем результаты
  Point2D actualPoint = line.getPoint(0);
  Vector2D actualDirection = line.getPoint(1) - line.getPoint(0);

  // Ожидаемые результаты:
  // Точка должна остаться на том же месте, так как поворот происходит вокруг нее
  EXPECT_EQ(expectedPoint, actualPoint);

  // Направление должно быть перпендикулярным начальному
  EXPECT_EQ(expectedDirection, actualDirection);
}


// Тест метода трансформации линии(Перемещения)
TEST(line2D, TransformMoveBase)
{
  Point2D startPoint{1.0, 3.0};
  Vector2D direction{3.0, 4.0};

  Line2D line(startPoint, direction);

  Vector2D offset{1.0, 1.0};

  Point2D expectedPoint{startPoint.x + offset.x, startPoint.y + offset.y};
  Vector2D expectedDirection = direction.normalized();

  line.transform(Matrix3::translate(offset));

  Point2D actualPoint = line.getPoint(0);
  Vector2D actualDirection = (line.getPoint(1) - line.getPoint(0)).normalized();

  EXPECT_EQ(expectedPoint, actualPoint);
  ;
  EXPECT_EQ(expectedDirection, actualDirection);
}


// Тест метода трансформации  линии(Поворот)
TEST(line2D, TransformRotateBase)
{
  Point2D startPoint{1.0, 3.0};
  Vector2D direction{3.0, 4.0};

  double angleRad = MathConstants::PI;

  double cos = std::cos(angleRad);
  double sin = std::sin(angleRad);

  Line2D line(startPoint, direction);
  line.transform(Matrix3::rotate(angleRad));

  Point2D actualPoint = line.getPoint(0);

  // Проверяем направляющий вектор
  Point2D pointT1 = line.getPoint(1);
  Vector2D actualDirection{pointT1.x - actualPoint.x, pointT1.y - actualPoint.y};
  actualDirection = actualDirection.normalized();

  double expectedPointX = startPoint.x * cos - startPoint.y * sin;
  double expectedPointY = startPoint.x * sin + startPoint.y * cos;
  double expectedDirX = direction.x * cos - direction.y * sin;
  double expectedDirY = direction.x * sin + direction.y * cos;

  Point2D expectedPoint{expectedPointX, expectedPointY};

  Vector2D expectedDirection{expectedDirX, expectedDirY};
  expectedDirection = expectedDirection.normalized();

  EXPECT_EQ(expectedPoint, actualPoint);
  // Вектор должен измениться
  EXPECT_EQ(expectedDirection, actualDirection);
}


//----------------------------------------------------
// НЕОРДИНАРНЫЕ СЛУЧАИ

// Передача нулевого вектора
TEST(line2D, ZeroVector)
{
  Point2D startPoint{1.0, 3.0};
  Vector2D zeroVector{0.0, 0.0};

  EXPECT_THROW(Line2D actual(startPoint, zeroVector), std::invalid_argument);
}


// Создание линии с отрицательным направляющим вектором
TEST(line2D, NegativeDirectionVector)
{
  Point2D startPoint{1.0, 3.0};
  Vector2D negativeDirection{-3.0, -4.0};

  Line2D line(startPoint, negativeDirection);

  Point2D actualPoint = line.getPoint(0);
  Point2D pointT1 = line.getPoint(1);
  Vector2D actualDirection{pointT1.x - actualPoint.x, pointT1.y - actualPoint.y};
  actualDirection = actualDirection.normalized();

  Vector2D expectedDirection = negativeDirection.normalized();

  EXPECT_EQ(startPoint, actualPoint);
  EXPECT_EQ(expectedDirection, actualDirection);
}


// Создание линии с большими инициализирующими значениями
TEST(line2D, LargeCoordinates)
{
  Point2D startPoint{1e6, 1e6};
  Vector2D direction{1e6, 1e6};

  Line2D line(startPoint, direction);

  Point2D actualPoint = line.getPoint(0);
  Point2D pointT1 = line.getPoint(1);
  Vector2D actualDirection{pointT1.x - actualPoint.x, pointT1.y - actualPoint.y};
  actualDirection = actualDirection.normalized();

  EXPECT_EQ(startPoint, actualPoint);
  EXPECT_EQ(direction.normalized(), actualDirection);
}


// Сдвиг на очень большую величину
TEST(line2D, TransformMoveLargeOffset)
{
  Point2D startPoint{1.0, 3.0};
  Vector2D direction{3.0, 4.0};

  Line2D line(startPoint, direction);

  Vector2D largeOffset{1e6, 1e6};

  Point2D expectedPoint{startPoint.x + largeOffset.x, startPoint.y + largeOffset.y};
  Vector2D expectedDirection = direction.normalized();

  line.transform(Matrix3::translate(largeOffset));

  Point2D actualPoint = line.getPoint(0);
  Vector2D actualDirection = (line.getPoint(1) - line.getPoint(0)).normalized();

  EXPECT_EQ(expectedPoint, actualPoint);
  EXPECT_EQ(expectedDirection, actualDirection);
}


// Поворот на 0 радиан
TEST(line2D, TransformRotateZeroAngle)
{
  Point2D startPoint{1.0, 3.0};
  Vector2D direction{3.0, 4.0};

  double angleRad = 0.0;

  Line2D line(startPoint, direction);
  line.transform(Matrix3::rotate(angleRad));

  Point2D actualPoint = line.getPoint(0);
  Vector2D actualDirection = (line.getPoint(1) - line.getPoint(0)).normalized();

  EXPECT_EQ(startPoint, actualPoint);
  EXPECT_EQ(direction.normalized(), actualDirection);
}


// Поворот на PI радиан
TEST(line2D, TransformRotate180Degrees)
{
  Point2D startPoint{1.0, 3.0};
  Vector2D direction{3.0, 4.0};

  double angleRad = MathConstants::PI;

  Line2D line(startPoint, direction);
  line.transform(Matrix3::rotate(angleRad));

  Point2D actualPoint = line.getPoint(0);
  Point2D pointT1 = line.getPoint(1);
  Vector2D actualDirection = pointT1 - actualPoint;
  actualDirection = actualDirection.normalized();

  Vector2D expectedDirection{-direction.x, -direction.y};

  EXPECT_EQ(expectedDirection.normalized(), actualDirection);
}
