#ifndef APPLICATIONFILESERVICE_H__
#define APPLICATIONFILESERVICE_H__

#include <memory>
#include <string>
#include <vector>

#include <io/objectpoolloader.h>
#include <io/objectpoolsaver.h>
#include <io/packingsceneloader.h>
#include <io/packingscenesaver.h>
#include <iview.h>

class Figure;

namespace ApplicationFileService
{
inline std::string openSaveFileDialog(const std::shared_ptr<IFileDialogView> & view, const std::string & title,
                                      const std::string & initPath, const std::string & filter = {})
{
  return view->openSaveFileDialog(title, initPath, filter);
}

inline std::string openLoadFileDialog(const std::shared_ptr<IFileDialogView> & view, const std::string & title,
                                      const std::string & initPath, const std::string & filter = {})
{
  return view->openLoadFileDialog(title, initPath, filter);
}

inline ObjectPoolLoader::Result loadFigurePool(const std::string & filePath)
{
  return ObjectPoolLoader::loadFromFile(filePath);
}

inline bool saveFigurePool(const std::string & filePath, const std::vector<std::shared_ptr<Figure>> & figures)
{
  return ObjectPoolSaver::saveToFile(filePath, figures);
}

inline bool savePackingScene(const std::string & filePath, const PackingSceneSnapshot & snapshot)
{
  return PackingSceneSaver::saveToFile(filePath, snapshot);
}

inline PackingSceneLoader::Result loadPackingScene(const std::string & filePath)
{
  return PackingSceneLoader::loadFromFile(filePath);
}
} // namespace ApplicationFileService

#endif
