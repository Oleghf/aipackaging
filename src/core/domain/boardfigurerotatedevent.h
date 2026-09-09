#ifndef FIGUREROTATEDEVENT_H__
#define FIGUREROTATEDDEVENT_H__

#include <boardevent.h>

////////////////////////////////////////////////////////////////////////////////
//
/// Событие поворота фигуры на доске
/**
*/
////////////////////////////////////////////////////////////////////////////////
class BoardFigureRotatedEvent : public BoardEvent
{
public:
  BoardFigureRotatedEvent(size_t boardID, size_t figureID)
    : BoardEvent(boardID)
    , figureID_(figureID)
  {
  }

  BoardEventType type() const override { return BoardEventType::BoardFigureRotated; }

  size_t figureID() const { return figureID_; }

private:
  const size_t figureID_;
};

#endif
