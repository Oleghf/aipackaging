#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "../support/utf8path.h"
#ifdef _WIN32
#define NOMINMAX
#include <shellapi.h>
#include <windows.h>
#endif

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
#ifdef _WIN32
      int count = 0;
      const auto release = [](wchar_t ** value) noexcept
      {
        LocalFree(value);
      };
      std::unique_ptr<wchar_t *, decltype(release)> wide(CommandLineToArgvW(GetCommandLineW(), &count), release);
      if (!wide)
        throw std::runtime_error("Не удалось прочитать аргументы командной строки");
      std::vector<std::string> encoded;
      encoded.reserve(static_cast<std::size_t>(count));
      for (int index = 0; index < count; ++index)
        encoded.push_back(aipackaging::files::utf8Path(std::filesystem::path(wide.get()[index])));
      std::vector<char *> values;
      for (auto & value : encoded)
        values.push_back(value.data());
      (void)arguments;
      return aipackaging::cli::runCli(count, values.data(), std::cout, std::cerr);
#else
      return aipackaging::cli::runCli(arguments->count, arguments->values, std::cout, std::cerr);
#endif
    },
    &arguments, std::cerr);
}
