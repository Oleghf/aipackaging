#include <gtest/gtest.h>
#include <point2d.h>
#include <vector2d.h>

// БАЗОВЫЕ СЛУЧАИ

// Тестирование оператора минус
TEST(point2D, OperatorMinusPointBase)
{
  Point2D p2{5.0, 2.0};
  Point2D p1{3.0, 1.0};

  Vector2D expected{2.0, 1.0};
  Vector2D actual = p2 - p1;

  EXPECT_EQ(expected, actual);
}


// Тестирование оператора плюс
TEST(point2D, OperatorPlusPointBase)
{
  Point2D p1{5.0, 2.0};
  Vector2D vec1{3.0, 1.0};

  Point2D expected{8.0, 3.0};
  Point2D actual = p1 + vec1;

  EXPECT_EQ(expected, actual);
}


// Тестирование оператора плюс равно
TEST(point2D, OperatorPlusAssignmentPointBase)
{
  Point2D actual{5.0, 2.0};
  Vector2D vec1{3.0, 1.0};

  Point2D expected{8.0, 3.0};
  actual += vec1;

  EXPECT_EQ(expected, actual);
}


// Тестирование оператора сравнения
TEST(point2D, OperatorEqual)
{
  Point2D p1{5.0, 3.0};
  Point2D p2{5.0, 3.0};
  Point2D p3{3.0, 1.0};

  EXPECT_TRUE(p1 == p2);
  EXPECT_FALSE(p1 == p3);
}


// Тестирование оператора обратного сравнению
TEST(point2D, OperatorNotEqual)
{
  Point2D p1{5.0, 3.0};
  Point2D p2{3.0, 1.0};

  EXPECT_TRUE(p1 != p2);
  EXPECT_FALSE(p1 != p1);
}

// -------------------------------------------------------
// НЕОРДИНАРНЫЕ СЛУЧАИ


// Вычитание одинаковых точек
TEST(point2D, SubtractEqualPoints)
{
  Point2D p1{5.0, 3.0};
  Point2D p2{5.0, 3.0};

  Vector2D expected{0.0, 0.0};
  Vector2D actual = p1 - p2;

  EXPECT_EQ(expected, actual);
}

// Сложение точки с нулевым вектором
TEST(point2D, AddZeroVector)
{
  Point2D p1{5.0, 3.0};
  Vector2D zeroVector{0.0, 0.0};

  Point2D expected{5.0, 3.0};
  Point2D actual = p1 + zeroVector;

  EXPECT_EQ(expected, actual);
}


// Сложение не инициализированных объектов
TEST(point2D, OperatorPlusNoInit)
{
  Point2D p1;
  Vector2D vec1;

  Point2D expected{0, 0};
  Point2D actual = p1 + vec1;

  EXPECT_EQ(expected, actual);
}


// Вычитание не инициализированных точек
TEST(point2D, OperatorMinusNoInit)
{
  Point2D p2;
  Point2D p1;

  Vector2D expected{0, 0};
  Vector2D actual = p2 - p1;

  EXPECT_EQ(expected, actual);
}

// Сложение с отрицательным вектором
TEST(point2D, AddNegativeVector)
{
  Point2D p1{5.0, 3.0};
  Vector2D negativeVector{-3.0, -1.0};

  Point2D expected{2.0, 2.0};
  Point2D actual = p1 + negativeVector;

  EXPECT_EQ(expected, actual);
}

// Сравнение не инициализированных точек
TEST(point2D, OperatorEqualNoInit)
{
  Point2D p1;
  Point2D p2;

  EXPECT_TRUE(p1 == p2);
}


//
TEST(point2D, OperatorNotEqualNoInit)
{
  Point2D p1;
  Point2D p2;

  EXPECT_FALSE(p1 != p2);
}
