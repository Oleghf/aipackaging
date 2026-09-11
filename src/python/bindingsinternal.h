#ifndef AIPACKAGING_PYTHON_BINDINGS_INTERNAL_H
#define AIPACKAGING_PYTHON_BINDINGS_INTERNAL_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <aipackaging/nesting/common_contracts.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

namespace aipackaging::python
{
namespace py = pybind11;

/// Создаёт владеющий read-only NumPy-массив указанной формы из C++-вектора.
template<typename T>
py::array_t<T> readonlyArray(const std::vector<T> & values, const std::vector<py::ssize_t> & shape)
{
  py::array_t<T> result(shape);
  if (!values.empty())
    std::memcpy(result.mutable_data(), values.data(), values.size() * sizeof(T));
  result.attr("setflags")(py::arg("write") = false);
  return result;
}

/// Создаёт настоящий read-only NumPy bool-массив из байтовой маски C++.
py::array_t<bool> readonlyBoolArray(const std::vector<std::uint8_t> & values, const std::vector<py::ssize_t> & shape);
/// Преобразует строгий Python-словарь provenance в общую metadata решения.
solver::SolverMetadata metadataFromDict(const py::dict & value);
/// Устанавливает общие атрибуты низкоуровневого Python-модуля.
void configureModule(py::module_ & module);
/// Регистрирует клеточные типы и функции в низкоуровневом Python-модуле.
void bindGrid(py::module_ & module);
/// Регистрирует полигональные типы и функции в низкоуровневом Python-модуле.
void bindPolygon(py::module_ & module);
} // namespace aipackaging::python

#endif
