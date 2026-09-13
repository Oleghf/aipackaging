#include <string>

#include "bindingsinternal.h"

namespace aipackaging::python
{
using namespace solver;

/// Преобразует байтовую маску C++ в логический массив NumPy без общего изменяемого буфера.
py::array_t<bool> readonlyBoolArray(const std::vector<std::uint8_t> & values, const std::vector<py::ssize_t> & shape)
{
  py::array_t<bool> result(shape);
  bool * output = result.mutable_data();
  for (std::size_t index = 0; index < values.size(); ++index)
    output[index] = values[index] != 0;
  result.attr("setflags")(py::arg("write") = false);
  return result;
}

/// Строго преобразует сведения Python о происхождении в публичные служебные данные решения C++ v2.
SolverMetadata metadataFromDict(const py::dict & value)
{
  static const std::vector<std::string> required = {"family",           "name",      "projectVersion",    "revision",  "seed",
                                                    "randomIterations", "beamWidth", "maxExpandedStates", "timeoutMs", "modelId",
                                                    "modelSha256",      "rollouts",  "selectionMode"};
  for (const std::string & field : required)
  {
    if (!value.contains(py::str(field)))
      throw py::value_error("в происхождении решения отсутствует обязательное поле: " + field);
  }
  if (value.size() != required.size())
    throw py::value_error("происхождение решения содержит неизвестные поля");

  SolverMetadata result;
  const std::string family = value["family"].cast<std::string>();
  if (!parseSolverFamily(family, result.family))
    throw py::value_error("неизвестное семейство решателя: " + family);
  result.name = value["name"].cast<std::string>();
  result.projectVersion = value["projectVersion"].cast<std::string>();
  result.revision = value["revision"].cast<std::string>();
  result.seed = value["seed"].cast<std::uint64_t>();
  result.randomIterations = value["randomIterations"].cast<std::size_t>();
  result.beamWidth = value["beamWidth"].cast<std::size_t>();
  result.maxExpandedStates = value["maxExpandedStates"].cast<std::size_t>();
  result.timeoutMs = value["timeoutMs"].cast<std::uint64_t>();
  result.modelId = value["modelId"].cast<std::string>();
  result.modelSha256 = value["modelSha256"].cast<std::string>();
  result.rollouts = value["rollouts"].cast<std::size_t>();
  result.selectionMode = value["selectionMode"].cast<std::string>();
  return result;
}

/// Настраивает описание и версию модуля из версии проекта, заданной при сборке.
void configureModule(py::module_ & module)
{
  module.doc() = "Низкоуровневые bindings сред раскроя AIPackaging";
  module.attr("__version__") = AIPACKAGING_PYTHON_VERSION;
  module.attr("__revision__") = AIPACKAGING_BUILD_REVISION;
}
} // namespace aipackaging::python
