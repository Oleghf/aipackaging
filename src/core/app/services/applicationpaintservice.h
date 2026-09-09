#ifndef APPLICATIONPAINTSERVICE_H__
#define APPLICATIONPAINTSERVICE_H__

#include <memory>

#include <iscenepainthandler.h>
#include <scenepaintevent.h>

namespace ApplicationPaintService
{
inline void paint(const std::shared_ptr<IScenePaintHandler> & paintHandler, const ScenePaintEvent & paintEvent)
{
  paintHandler->onPaintEvent(paintEvent);
}
} // namespace ApplicationPaintService

#endif
