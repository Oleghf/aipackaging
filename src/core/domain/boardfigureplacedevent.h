#ifndef BOARDFIGUREPLACEDEVENT_H__
#define BOARDFIGUREPLACEDEVENT_H__

#include <boardevent.h>

////////////////////////////////////////////////////////////////////////////////
//
/// Событие помещения фигуры на доску
/**
*/
////////////////////////////////////////////////////////////////////////////////
class BoardFigurePlacedEvent : public BoardEvent
{
public:
  BoardFigurePlacedEvent(size_t boardID, size_t figureID)
    : BoardEvent(boardID)
    , figureID_(figureID)
  {
  }

  BoardEventType type() const override { return BoardEventType::BoardFigurePlaced; }

  inline size_t figureID() const { return figureID_; }

private:
  const size_t figureID_;
};

#endif
