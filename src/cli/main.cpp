#include <iostream>

#include <cli_boundary.h>
#include <cli_commands.h>

/// Передаёт весь разбор и выполнение в невыбрасывающую границу процесса CLI.
int main(int argc, char ** argv)
{
  /// Хранит невладеющие аргументы процесса на время синхронного вызова границы.
  struct Arguments
  {
    int count;
    char ** values;
  } arguments{argc, argv};
  return aipackaging::cli::runCliGuarded(
    [](void * context)
    {
      const auto * arguments = static_cast<const Arguments *>(context);
      return aipackaging::cli::runCli(arguments->count, arguments->values, std::cout, std::cerr);
    },
    &arguments, std::cerr);
}
