#ifndef OBJECTPOOLSAVER_H__
#define OBJECTPOOLSAVER_H__

#include <memory>
#include <string>
#include <vector>

class Figure;

namespace ObjectPoolSaver
{
// Сохранить пул фигур в файл
bool saveToFile(const std::string & filePath, const std::vector<std::shared_ptr<Figure>> & figures);
// Преобразовать пул фигур в текст
std::string saveToText(const std::vector<std::shared_ptr<Figure>> & figures);
} // namespace ObjectPoolSaver

#endif
