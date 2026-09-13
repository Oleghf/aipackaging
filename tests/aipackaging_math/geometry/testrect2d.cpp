#include <gtest/gtest.h>
#include <mathutils.h>
#include <rect2d.h>


// БАЗОВЫЕ СЛУЧАИ


// Базовый тест пересечения
TEST(rect2D, intersectsBase)
{
  Rect2D rect{{0, 1}, {2, 0}};
  Rect2D intersectedRect{{0.5, 1.5}, {1.5, -0.5}};

  bool expected = true;
  bool actual = rect.intersects(intersectedRect);

  EXPECT_EQ(expected, actual);
}


// Базовый тест проверки вхождения
TEST(rect2D, ContainsBase)
{
  Rect2D rect{{0, 0}, {10, -10}};

  // Прямоугольник, полностью находящийся внутри другого прямоугольника
  Rect2D containedRect{{2, -2}, {8, -8}};

  bool expected = true;
  bool actual = rect.contains(containedRect);

  EXPECT_EQ(expected, actual);
}


// Базовый тест оператора сравнения
TEST(rect2D, operatorEqual)
{
  Rect2D rect1{{2, 0}, {5, -5}};
  Rect2D rect2{{2.0}, {5, -5}};
  Rect2D rect3{{2, 0}, {5, -4}};

  EXPECT_TRUE(rect1 == rect2);
  EXPECT_FALSE(rect1 == rect3);
}


// Базовый тест оператора !=
TEST(rect2D, operatorNotEqual)
{
  Rect2D rect1{{2, 0}, {5, -5}};
  Rect2D rect2{{2.0}, {5, -5}};
  Rect2D rect3{{2, 0}, {5, -4}};

  EXPECT_TRUE(rect1 != rect3);
  EXPECT_FALSE(rect1 != rect2);
}


// --------------------------------------------
// НЕОРДИНАРНЫЕ СЛУЧАИ

// Проверка метода пересечения при полном вхождении объекта
TEST(rect2D, intersectsEqualContains)
{
  Rect2D rect{{0, 0}, {10, -10}};
  Rect2D intersectedRect{{1, -1}, {9, -9}};

  bool expected = true;
  bool actual = rect.intersects(intersectedRect);

  EXPECT_EQ(expected, actual);
}


// Проверка результата метода содержания при пересечении с прямоугольником
TEST(rect2D, containsNotEqualIntersects)
{
  Rect2D rect{{0, 0}, {10, -10}};

  // Прямоугольник, частично находящийся внутри другого прямоугольника
  Rect2D intersectedRect{{-1, 0}, {8, -8}};

  bool expected = false;
  bool actual = rect.contains(intersectedRect);

  EXPECT_EQ(expected, actual);
}
