#include <coordinates2d.h>

//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
Coordinates::Coordinates(int column, int row)
  : column(column)
  , row(row)
{
}


//------------------------------------------------------------------------------
/**
  Определенный оператор ==
*/
//--
bool Coordinates::operator==(const Coordinates & other) const
{
  return column == other.column && row == other.row;
}


//------------------------------------------------------------------------------
/**
  Определенный оператор !=
*/
//--
bool Coordinates::operator!=(const Coordinates & other) const
{
  return !(*this == other);
}
