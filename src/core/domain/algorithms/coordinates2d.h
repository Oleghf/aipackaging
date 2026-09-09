#ifndef COORDINATES2D_H
#define COORDINATES2D_H

////////////////////////////////////////////////////////////////////////////////
//
/// Координаты клетки
/**
*/
////////////////////////////////////////////////////////////////////////////////
struct Coordinates
{
public:
  Coordinates(int column, int row);
  bool operator==(const Coordinates & other) const;
  bool operator!=(const Coordinates & other) const;

public:
  int column;
  int row;
};

#endif
