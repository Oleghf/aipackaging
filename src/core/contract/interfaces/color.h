#ifndef COLOR_H
#define COLOR_H

////////////////////////////////////////////////////////////////////////////////
//
/// Цвет
/**
*/
////////////////////////////////////////////////////////////////////////////////
struct Color
{
  Color(unsigned int r_, unsigned int g_, unsigned int b_, unsigned int a_ = 255)
    : r(r_)
    , g(g_)
    , b(b_)
    , a(a_)
  {
  }

  // Константный набор цветов
  static Color BLACK() { return {0, 0, 0}; }
  static Color GREEN() { return {0, 255, 0}; }
  static Color RED() { return {255, 0, 0}; }
  static Color BLUE() { return {0, 0, 255}; }
  static Color TRANSPARENT() { return {0, 0, 0, 0}; }

public:
  unsigned int r;
  unsigned int g;
  unsigned int b;
  unsigned int a;
};

#endif
