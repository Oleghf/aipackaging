#ifndef AIPACKAGING_CLI_CLI_BOUNDARY_H
#define AIPACKAGING_CLI_CLI_BOUNDARY_H

#include <iosfwd>

namespace aipackaging::cli
{
/// Представляет вызываемую без владения операцию верхнего уровня CLI.
using CliOperation = int (*)(void * context);

/// Выполняет операцию и преобразует любое исключение в русскую диагностику и код 1.
int runCliGuarded(CliOperation operation, void * context, std::ostream & diagnostics) noexcept;
} // namespace aipackaging::cli

#endif
