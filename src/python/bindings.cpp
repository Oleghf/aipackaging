#include "bindingsinternal.h"

/// Создаёт модуль и делегирует регистрацию независимым grid/polygon адаптерам.
PYBIND11_MODULE(_aipackaging_solver, module)
{
  aipackaging::python::configureModule(module);
  aipackaging::python::bindGrid(module);
  aipackaging::python::bindPolygon(module);
}
