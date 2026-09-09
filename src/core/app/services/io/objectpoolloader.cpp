#include <fstream>
#include <sstream>
#include <string>

#include <cell2d.h>
#include <io/objectpoolloader.h>
#include <objectfigure.h>
#include <point2d.h>

namespace
{
constexpr Point2D DEFAULT_FIGURE_TOP_LEFT{5, -5};

std::shared_ptr<Figure> makeObjectFigure(const std::vector<Coordinates> & coordinates)
{
  std::vector<Cell2D> cells;
  cells.reserve(coordinates.size());

  for (const Coordinates & coords : coordinates)
    cells.push_back(CreateSquareCell(coords));

  return std::make_shared<ObjectFigure>(DEFAULT_FIGURE_TOP_LEFT, std::move(cells));
}
} // namespace


//------------------------------------------------------------------------------
/**
  Загружает пул фигур из файла
*/
//--
ObjectPoolLoader::Result ObjectPoolLoader::loadFromFile(const std::string & filePath)
{
  std::ifstream input(filePath);
  if (!input.is_open())
    return {false, {}, "Не удалось открыть файл пула фигур"};

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return loadFromText(buffer.str());
}


//------------------------------------------------------------------------------
/**
  Загружает пул фигур из текста
*/
//--
ObjectPoolLoader::Result ObjectPoolLoader::loadFromText(const std::string & text)
{
  std::istringstream input(text);
  std::string line;

  std::vector<std::shared_ptr<Figure>> figures;
  std::vector<Coordinates> currentObjectCells;
  bool isInsideObject = false;

  while (std::getline(input, line))
  {
    if (line.empty())
      continue;

    std::istringstream lineStream(line);
    std::string token;
    lineStream >> token;

    if (token == "OBJECT")
    {
      if (isInsideObject)
        return {false, {}, "Вложенные блоки OBJECT не поддерживаются"};

      isInsideObject = true;
      currentObjectCells.clear();
      continue;
    }

    if (token == "END_OBJECT")
    {
      if (!isInsideObject)
        return {false, {}, "END_OBJECT без блока OBJECT"};
      if (currentObjectCells.empty())
        return {false, {}, "Блок OBJECT должен содержать хотя бы одну CELL"};

      figures.push_back(makeObjectFigure(currentObjectCells));
      currentObjectCells.clear();
      isInsideObject = false;
      continue;
    }

    if (token == "CELL")
    {
      if (!isInsideObject)
        return {false, {}, "CELL должна находиться внутри блока OBJECT"};

      int column = 0;
      int row = 0;
      if (!(lineStream >> column >> row))
        return {false, {}, "Строка CELL должна содержать колонку и строку"};

      currentObjectCells.emplace_back(column, row);
      continue;
    }

    return {false, {}, "Неизвестная строка в файле пула фигур"};
  }

  if (isInsideObject)
    return {false, {}, "Блок OBJECT не закрыт"};

  return {true, std::move(figures), {}};
}
