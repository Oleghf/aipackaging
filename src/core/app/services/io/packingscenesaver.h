#ifndef PACKINGSCENESAVER_H__
#define PACKINGSCENESAVER_H__

#include <string>

#include <packing/packingscenesnapshot.h>

namespace PackingSceneSaver
{
// Сохранить снимок сцены упаковки в файл
bool saveToFile(const std::string & filePath, const PackingSceneSnapshot & snapshot);
// Преобразовать снимок сцены упаковки в текст
std::string saveToText(const PackingSceneSnapshot & snapshot);
} // namespace PackingSceneSaver

#endif
