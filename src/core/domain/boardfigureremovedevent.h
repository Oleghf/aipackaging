#ifndef BOARDFIGUREREMOVEDEVENT_H__
#define BOARDFIGUREREMOVEDEVENT_H__

#include <boardevent.h>

////////////////////////////////////////////////////////////////////////////////
//
/// Событие удаление фигуры с доски
/**
*/
////////////////////////////////////////////////////////////////////////////////
class BoardFigureRemovedEvent : public BoardEvent
{
public:
  BoardFigureRemovedEvent(size_t boardID, size_t figureID)
    : BoardEvent(boardID)
    , figureID_(figureID)
  {
  }

  BoardEventType type() const override { return BoardEventType::BoardFigureRemoved; }

  inline size_t figureID() const { return figureID_; }

private:
  const size_t figureID_;
};

#endif
