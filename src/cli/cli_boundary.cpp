#include <exception>
#include <ostream>

#include <cli_boundary.h>

namespace aipackaging::cli
{
namespace
{
/// Пытается вывести диагностику, не позволяя ошибке потока покинуть невыбрасывающую границу.
void writeDiagnostic(std::ostream & output, const char * prefix, const char * detail = nullptr) noexcept
{
  try
  {
    output << prefix;
    if (detail != nullptr)
      output << detail;
    output << '\n';
  }
  catch (...)
  {
    // Диагностика имеет вспомогательный характер: сбой потока не должен нарушить
    // невыбрасывающую границу CLI.
    return;
  }
}
} // namespace

/// Вызывает операцию через указатель и последовательно перехватывает стандартные и неизвестные исключения.
int runCliGuarded(CliOperation operation, void * context, std::ostream & diagnostics) noexcept
{
  try
  {
    return operation(context);
  }
  catch (const std::exception & error)
  {
    writeDiagnostic(diagnostics, "Внутренняя ошибка: ", error.what());
    return 1;
  }
  catch (...)
  {
    writeDiagnostic(diagnostics, "Неизвестная внутренняя ошибка");
    return 1;
  }
}
} // namespace aipackaging::cli
