#ifndef BOARDEVENT_H__
#define BOARDEVENT_H__

#include <cstddef>

enum class BoardEventType
{
  BoardFigureMoved,
  BoardFigurePlaced,
  BoardFigureRemoved,
  BoardFigureRotated
};

////////////////////////////////////////////////////////////////////////////////
//
/// Базовый класс события доски
/**
*/
////////////////////////////////////////////////////////////////////////////////
class BoardEvent
{
public:
  BoardEvent(size_t boardID)
    : boardID_(boardID)
  {
  }
  virtual ~BoardEvent() = default;

  inline size_t boardID() const { return boardID_; }
  virtual BoardEventType type() const = 0;

private:
  const size_t boardID_;
};

#endif
