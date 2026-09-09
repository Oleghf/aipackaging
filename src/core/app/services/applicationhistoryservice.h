#ifndef APPLICATIONHISTORYSERVICE_H__
#define APPLICATIONHISTORYSERVICE_H__

#include <memory>

#include <commandmanager.h>
#include <iview.h>

namespace ApplicationHistoryService
{
inline void undo(const std::shared_ptr<CommandManager> & commandManager, const std::shared_ptr<IRedrawView> & view)
{
  commandManager->undo();
  view->requestRedraw();
}

inline void redo(const std::shared_ptr<CommandManager> & commandManager, const std::shared_ptr<IRedrawView> & view)
{
  commandManager->redo();
  view->requestRedraw();
}
} // namespace ApplicationHistoryService

#endif
