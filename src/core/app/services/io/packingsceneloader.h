#ifndef PACKINGSCENELOADER_H__
#define PACKINGSCENELOADER_H__

#include <string>

#include <packing/packingscenesnapshot.h>

////////////////////////////////////////////////////////////////////////////////
//
/// Загрузчик сцены упаковки
/**
*/
////////////////////////////////////////////////////////////////////////////////
class PackingSceneLoader
{
public:
  struct Result
  {
    bool success = false;
    PackingSceneSnapshot snapshot;
    std::string errorMessage;
  };

  // Загрузить сцену упаковки из файла
  static Result loadFromFile(const std::string & filePath);
  // Загрузить сцену упаковки из текста
  static Result loadFromText(const std::string & text);
};

#endif
