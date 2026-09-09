#include <fstream>
#include <sstream>

#include <algs.h>
#include <io/objectpoolsaver.h>

//------------------------------------------------------------------------------
/**
  Сохраняет пул фигур в файл
*/
//--
bool ObjectPoolSaver::saveToFile(const std::string & filePath, const std::vector<std::shared_ptr<Figure>> & figures)
{
  std::ofstream output(filePath);
  if (!output.is_open())
    return false;

  output << saveToText(figures);
  return true;
}


//------------------------------------------------------------------------------
/**
  Преобразует пул фигур в текстовый OBJECT формат
*/
//--
std::string ObjectPoolSaver::saveToText(const std::vector<std::shared_ptr<Figure>> & figures)
{
  std::ostringstream output;

  for (const std::shared_ptr<Figure> & figure : figures)
  {
    output << "OBJECT\n";
    for (const Cell2D & cell : figure->GetCells())
    {
      const Coordinates coords = cell.GetCoordinates();
      output << "CELL " << coords.column << ' ' << coords.row << '\n';
    }
    output << "END_OBJECT\n";
  }

  return output.str();
}
