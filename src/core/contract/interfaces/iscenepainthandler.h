#ifndef ISCENEPAINTHANDLER_H__
#define ISCENEPAINTHANDLER_H__

class ScenePaintEvent;

class IScenePaintHandler
{
public:
  virtual ~IScenePaintHandler() = default;
  virtual void onPaintEvent(const ScenePaintEvent & event) = 0;
};

#endif
