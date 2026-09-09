#include <cassert>

#include <point2d.h>
#include <scenemouseevent.h>


//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
SceneMouseEvent::SceneMouseEvent(EventType type, const Point2D & scenePos, MouseButton button)
  : type_(type)
  , scenePos_(scenePos)
  , button_(button)
{
  assert(type == EventType::MousePress || type == EventType::MouseMove || type == EventType::MouseRelease);
}
