#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <io/packingscenesaver.h>

namespace
{
std::string escapeJsonString(const std::string & value)
{
  std::ostringstream output;

  for (const char ch : value)
  {
    switch (ch)
    {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        output << ch;
        break;
    }
  }

  return output.str();
}

std::string formatJsonNumber(double value)
{
  std::ostringstream output;

  if (std::floor(value) == value)
    output << std::fixed << std::setprecision(1) << value;
  else
    output << std::setprecision(10) << value;

  return output.str();
}
} // namespace


//------------------------------------------------------------------------------
/**
  Сохраняет снимок сцены раскроя в файл JSON
*/
//--
bool PackingSceneSaver::saveToFile(const std::string & filePath, const PackingSceneSnapshot & snapshot)
{
  std::ofstream output(filePath);
  if (!output.is_open())
    return false;

  output << saveToText(snapshot);
  return true;
}


//------------------------------------------------------------------------------
/**
  Преобразует снимок сцены раскроя в текст JSON
*/
//--
std::string PackingSceneSaver::saveToText(const PackingSceneSnapshot & snapshot)
{
  std::ostringstream output;

  output << "{\n";
  output << "  \"format\": \"aipackaging.packing_scene\",\n";
  output << "  \"version\": 1,\n";
  output << "  \"board\": { \"columns\": " << snapshot.board.columns << ", \"rows\": " << snapshot.board.rows << ", \"unit\": \""
         << escapeJsonString(snapshot.board.unit) << "\" },\n";

  output << "  \"objects\": [\n";
  for (size_t objectIndex = 0; objectIndex < snapshot.objects.size(); ++objectIndex)
  {
    const PackingSceneObject & object = snapshot.objects[objectIndex];

    output << "    {\n";
    output << "      \"id\": \"" << escapeJsonString(object.id) << "\",\n";
    output << "      \"geometry\": {\n";
    output << "        \"type\": \"cell_grid\",\n";
    output << "        \"cells\": [\n";

    for (size_t cellIndex = 0; cellIndex < object.cells.size(); ++cellIndex)
    {
      const PackingSceneCell & cell = object.cells[cellIndex];
      output << "          { \"column\": " << cell.column << ", \"row\": " << cell.row << " }";
      if (cellIndex + 1 < object.cells.size())
        output << ',';
      output << '\n';
    }

    output << "        ]\n";
    output << "      }\n";
    output << "    }";
    if (objectIndex + 1 < snapshot.objects.size())
      output << ',';
    output << '\n';
  }
  output << "  ],\n";

  output << "  \"actions\": [\n";
  for (size_t actionIndex = 0; actionIndex < snapshot.actions.size(); ++actionIndex)
  {
    const PackingSceneAction & action = snapshot.actions[actionIndex];

    output << "    {\n";
    output << "      \"type\": \"" << escapeJsonString(action.type) << "\",\n";
    output << "      \"objectId\": \"" << escapeJsonString(action.objectId) << "\",\n";
    output << "      \"x\": " << formatJsonNumber(action.x) << ",\n";
    output << "      \"y\": " << formatJsonNumber(action.y) << ",\n";
    output << "      \"rotationDegrees\": " << formatJsonNumber(action.rotationDegrees) << '\n';
    output << "    }";
    if (actionIndex + 1 < snapshot.actions.size())
      output << ',';
    output << '\n';
  }
  output << "  ]\n";
  output << "}\n";

  return output.str();
}
