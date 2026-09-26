#ifndef AIPACKAGING_CLI_ARGUMENTS_H
#define AIPACKAGING_CLI_ARGUMENTS_H

#include <span>
#include <string_view>

#include <cli_types.h>

namespace aipackaging::cli
{
/// Разбирает аргументы процесса без чтения файлов, вывода сообщений и запуска команд.
ParseResult parseArguments(std::span<const std::string_view> arguments);
} // namespace aipackaging::cli

#endif
