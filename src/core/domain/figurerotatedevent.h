#ifndef FIGUREROTATEDEVENT_H__
#define FIGUREROTATEDDEVENT_H__

#include <figureevent.h>

////////////////////////////////////////////////////////////////////////////////
//
/// Событие поворота фигуры
/**
*/
////////////////////////////////////////////////////////////////////////////////
class FigureRotatedEvent : public FigureEvent
{
public:
  FigureRotatedEvent(size_t figureID)
    : FigureEvent(figureID)
  {
  }

  FigureEventType type() const override { return FigureEventType::FigureRotated; }
};

#endif
