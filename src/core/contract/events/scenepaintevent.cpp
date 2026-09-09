#include <scenepaintevent.h>


//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
ScenePaintEvent::ScenePaintEvent(PrimitiveView & painter, ScenePaint whatScene, const Rect2D & region)
  : painter_(painter)
  , whatScene_(whatScene)
  , region_(region)
{
}
