#ifndef OBJECTPOOLLOADER_H__
#define OBJECTPOOLLOADER_H__

#include <memory>
#include <string>
#include <vector>

class Figure;

namespace ObjectPoolLoader
{
struct Result
{
  bool success = false;
  std::vector<std::shared_ptr<Figure>> figures;
  std::string errorMessage;
};

// Загрузить пул фигур из файла
Result loadFromFile(const std::string & filePath);
// Загрузить пул фигур из текста
Result loadFromText(const std::string & text);
} // namespace ObjectPoolLoader

#endif
