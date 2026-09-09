#include <gtest/gtest.h>
#include <qtadapters.h>
#include <scenemouseevent.h>


//------------------------------------------------------------------------------
/**
  Проверяет преобразование одиночных кнопок мыши Qt в контрактные кнопки
*/
//--
TEST(QtAdapters, FromQtMouseButtonMapsSingleButtons)
{
  EXPECT_EQ(QtAdapters::fromQtMouseButton(Qt::MouseButtons(Qt::LeftButton)), MouseButton::Left);
  EXPECT_EQ(QtAdapters::fromQtMouseButton(Qt::MouseButtons(Qt::RightButton)), MouseButton::Right);
  EXPECT_EQ(QtAdapters::fromQtMouseButton(Qt::MouseButtons(Qt::MiddleButton)), MouseButton::Middle);
}


//------------------------------------------------------------------------------
/**
  Проверяет приоритет левой кнопки при одновременном нажатии нескольких кнопок
*/
//--
TEST(QtAdapters, FromQtMouseButtonPrefersLeftButton)
{
  const Qt::MouseButtons buttons = Qt::MouseButtons(Qt::LeftButton) | Qt::MouseButtons(Qt::RightButton);

  EXPECT_EQ(QtAdapters::fromQtMouseButton(buttons), MouseButton::Left);
}
