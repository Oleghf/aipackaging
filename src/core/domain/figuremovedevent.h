#ifndef FIGUREMOVEDEVENT_H__
#define FIGUREMOVEDEVENT_H__

#include <vector>

#include <coordinates2d.h>
#include <figureevent.h>

////////////////////////////////////////////////////////////////////////////////
//
/// Событие передвижения фигуры
/**
*/
////////////////////////////////////////////////////////////////////////////////
class FigureMovedEvent : public FigureEvent
{
public:
  FigureMovedEvent(size_t figureID, const std::vector<Coordinates> & from, const std::vector<Coordinates> & to)
    : FigureEvent(figureID)
    , from_(from)
    , to_(to)
  {
  }

  FigureEventType type() const override { return FigureEventType::FigureMoved; }

  inline const std::vector<Coordinates> & from() const { return from_; }
  inline const std::vector<Coordinates> & to() const { return to_; }

private:
  const std::vector<Coordinates> from_;
  const std::vector<Coordinates> to_;
};

#endif
