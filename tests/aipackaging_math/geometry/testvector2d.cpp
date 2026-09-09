#include <gtest/gtest.h>
#include <mathutils.h>
#include <vector2d.h>

// БАЗОВЫЕ СЛУЧАИ

// Тест метода возвращающего длину
TEST(vector2D, LengthBase)
{
  Vector2D v{3.0, 4.0};

  double expected = 5.0;
  double actual = v.length();

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}

// Тест метода нормализации
TEST(vector2D, NormalizedBase)
{
  // Исходный вектор
  Vector2D vector{3.0, 4.0};

  // Нормализованный вектор
  Vector2D actual = vector.normalized();

  Vector2D expected{3.0 / 5.0, 4.0 / 5.0};

  EXPECT_EQ(expected, actual);
}

// Тест метода скалярного произведения векторов
TEST(vector2D, DotProductBase)
{
  Vector2D v{3.0, 4.0};
  Vector2D otherV{3.0, 4.0};

  double expected = v.x * otherV.x + v.y * otherV.y;
  double actual = v.dotProduct(otherV);

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}

// Тест оператора сложения
TEST(vector2D, OperatorPlusVectorBase)
{
  Vector2D v{3.0, 4.0};
  Vector2D otherV{3.0, 4.0};

  Vector2D expected{6.0, 8.0};
  Vector2D actual = v + otherV;

  EXPECT_EQ(expected, actual);
}

// Тест оператора сложения-присвоения
TEST(vector2D, OperatorPlusAssignmentVectorBase)
{
  Vector2D actual{3.0, 4.0};
  Vector2D otherV{3.0, 4.0};

  Vector2D expected{6.0, 8.0};
  actual += otherV;

  EXPECT_EQ(expected, actual);
}

// Тест оператора вычитания
TEST(vector2D, OperatorMinusVectorBase)
{
  Vector2D v{3.0, 4.0};
  Vector2D otherV{3.0, 4.0};

  Vector2D expected{0.0, 0.0};
  Vector2D actual = v - otherV;

  EXPECT_EQ(expected, actual);
}

// Тест оператора вычитания-присваивания
TEST(vector2D, OperatorMinusAssignmentVectorBase)
{
  Vector2D actual{3.0, 4.0};
  Vector2D otherV{3.0, 4.0};

  Vector2D expected{0, 0};
  actual -= otherV;

  EXPECT_EQ(expected, actual);
}

// Тест оператора умножения на скаляр
TEST(vector2D, OperatorMultiplyScalarBase)
{
  Vector2D v{3.0, 4.0};
  double scalar = 2.0;

  Vector2D expected{6.0, 8.0};
  Vector2D actual = v * scalar;

  EXPECT_EQ(expected, actual);
}

// Тест оператора умножения-присваивания скаляра
TEST(vector2D, OperatorMultiplyAssignmentScalarBase)
{
  Vector2D actual{3.0, 4.0};
  double scalar = 2.0;

  Vector2D expected{6.0, 8.0};
  actual *= scalar;

  EXPECT_EQ(expected, actual);
}

// Тест оператора сравнения
TEST(vector2D, OperatorEqual)
{
  Vector2D v1{3.0, 4.0};
  Vector2D v2{3.0, 4.0};
  Vector2D v3{5.0, 6.0};

  EXPECT_TRUE(v1 == v2);
  EXPECT_FALSE(v1 == v3);
}

// Тест оператора обратного сравнению
TEST(vector2D, OperatorNotEqual)
{
  Vector2D v1{3.0, 4.0};
  Vector2D v2{5.0, 6.0};
  Vector2D v3{3.0, 4.0};

  EXPECT_TRUE(v1 != v2);
  EXPECT_FALSE(v1 != v3);
}

// ---------------------------------------------------------
// НЕОРДИНАРНЫЕ СЛУЧАИ

// Попытка нормализации нулевого вектора
TEST(vector2D, NormalizeZero)
{
  Vector2D zeroVector{0, 0};

  EXPECT_THROW(zeroVector.normalized(), std::invalid_argument);
}

// Скалярное произведение с нулевым вектором
TEST(vector2D, DotProductWithZeroVector)
{
  Vector2D v{3.0, 4.0};
  Vector2D zeroVector{0.0, 0.0};

  double expected = 0.0;
  double actual = v.dotProduct(zeroVector);

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}

// Сложение с нулевым вектором
TEST(vector2D, OperatorPlusZeroVector)
{
  Vector2D v{3.0, 4.0};
  Vector2D zeroVector{0.0, 0.0};

  Vector2D expected{3.0, 4.0};
  Vector2D actual = v + zeroVector;

  EXPECT_EQ(expected, actual);
}

// Вычитание нулевого вектора
TEST(vector2D, OperatorMinusZeroVector)
{
  Vector2D v{3.0, 4.0};
  Vector2D zeroVector{0.0, 0.0};

  Vector2D expected{3.0, 4.0};
  Vector2D actual = v - zeroVector;

  EXPECT_EQ(expected, actual);
}


// Поиск длины неинициализированного вектора
TEST(vector2D, lengthNoInit)
{
  Vector2D vec;

  double expected = 0;
  double actual = vec.length();

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}


// Нормализация не инициализированного вектора
TEST(vector2D, normalizeNoInit)
{
  Vector2D vec;

  EXPECT_THROW(vec.normalized(), std::invalid_argument);
}


// Скалярное произведение не инициализированных векторов
TEST(vector2D, DotProductNoInit)
{
  Vector2D vec1;
  Vector2D vec2;

  double expected = 0;
  double actual = vec1.dotProduct(vec2);

  EXPECT_NEAR(expected, actual, MathConstants::TOLERANCE_DOUBLE);
}
