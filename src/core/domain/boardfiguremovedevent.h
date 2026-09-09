#ifndef BOARDFIGUREMOVEDEVENT_H__
#define BOARDFIGUREMOVEDEVENT_H__

#include <vector>

#include <boardevent.h>
#include <coordinates2d.h>

////////////////////////////////////////////////////////////////////////////////
//
/// Событие передвижения фигуры на доске
/**
*/
////////////////////////////////////////////////////////////////////////////////
class BoardFigureMovedEvent : public BoardEvent
{
public:
  BoardFigureMovedEvent(size_t boardID, size_t figureID, const std::vector<Coordinates> & from,
                        const std::vector<Coordinates> & to)
    : BoardEvent(boardID)
    , figureID_(figureID)
    , from_(from)
    , to_(to)
  {
  }

  BoardEventType type() const override { return BoardEventType::BoardFigureMoved; }

  inline size_t figureID() const { return figureID_; }
  inline const std::vector<Coordinates> & from() const { return from_; }
  inline const std::vector<Coordinates> & to() const { return to_; }

private:
  const size_t figureID_;
  const std::vector<Coordinates> from_;
  const std::vector<Coordinates> to_;
};

#endif
