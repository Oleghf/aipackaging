#ifndef SCENEPAINTEVENT_H__
#define SCENEPAINTEVENT_H__

#include <event.h>
#include <rect2d.h>

class PrimitiveView;

////////////////////////////////////////////////////////////////////////////////
//
/// Перечисление сцен, которые могут быть отрисованы
/**
*/
////////////////////////////////////////////////////////////////////////////////
enum class ScenePaint
{
  Main,
  Generate
};


////////////////////////////////////////////////////////////////////////////////
//
/// Событие перерисовки сцены
/**
*/
////////////////////////////////////////////////////////////////////////////////
class ScenePaintEvent : public Event
{
public:
  ScenePaintEvent(PrimitiveView & painter, ScenePaint whatScene, const Rect2D & region);

  EventType type() const override { return EventType::Paint; }

  // Несет контекст с какой сцены пришел запрос на отрисовку
  ScenePaint whatScene() const { return whatScene_; }

  // Отрисовщик
  PrimitiveView & painter() const { return painter_; }

  // Обрамляющий прямоугольник области отрисовки
  Rect2D region() const { return region_; }

private:
  PrimitiveView & painter_;
  const ScenePaint whatScene_;
  const Rect2D region_;
};

#endif
