
#include <gtest/gtest.h>

/// Инициализирует GoogleTest и возвращает итоговый код выполнения всех зарегистрированных тестов.
int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
