#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <cli_arguments.h>
#include <cli_commands.h>
#include <gtest/gtest.h>

namespace
{
using namespace aipackaging::cli;

/// Разбирает удобный для тестов список строк как полный массив аргументов процесса.
ParseResult parse(std::initializer_list<std::string_view> arguments)
{
  return parseArguments(std::vector<std::string_view>(arguments));
}

/// Вызывает полный контур CLI с изменяемыми указателями, требуемыми контрактом `main`.
int invoke(std::vector<std::string> arguments, std::ostringstream & output, std::ostringstream & diagnostics)
{
  std::vector<char *> values;
  values.reserve(arguments.size());
  for (std::string & argument : arguments)
    values.push_back(argument.data());
  return runCli(static_cast<int>(values.size()), values.data(), output, diagnostics);
}
} // namespace

/// Проверяет значения по умолчанию и независимость результата от порядка именованных параметров.
TEST(CliArguments, ParsesSolveDefaultsAndNamedOptions)
{
  const ParseResult defaults = parse({"cli", "solve", "--input", "in.json", "--output", "out.json"});
  ASSERT_TRUE(defaults.command.has_value());
  const auto & defaultCommand = std::get<SolveCommand>(*defaults.command);
  EXPECT_EQ(defaultCommand.solverName, "area-left-bottom");
  EXPECT_EQ(defaultCommand.seed, 42U);
  EXPECT_EQ(defaultCommand.randomIterations, 64U);
  EXPECT_EQ(defaultCommand.beamWidth, 32U);
  EXPECT_EQ(defaultCommand.maxExpandedStates, 50000U);
  EXPECT_EQ(defaultCommand.timeoutMs, 30000U);
  EXPECT_EQ(defaultCommand.neuralRollouts, 16U);

  const ParseResult configured = parse({"cli",
                                        "solve",
                                        "--timeout-ms",
                                        "0",
                                        "--beam-width",
                                        "7",
                                        "--output",
                                        "out.json",
                                        "--seed",
                                        "18446744073709551615",
                                        "--input",
                                        "in.json",
                                        "--random-iterations",
                                        "9",
                                        "--max-expanded-states",
                                        "11",
                                        "--rollouts",
                                        "3",
                                        "--solver",
                                        "beam"});
  ASSERT_TRUE(configured.command.has_value());
  const auto & command = std::get<SolveCommand>(*configured.command);
  EXPECT_EQ(command.solverName, "beam");
  EXPECT_EQ(command.seed, std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ(command.randomIterations, 9U);
  EXPECT_EQ(command.beamWidth, 7U);
  EXPECT_EQ(command.maxExpandedStates, 11U);
  EXPECT_EQ(command.timeoutMs, 0U);
  EXPECT_EQ(command.neuralRollouts, 3U);
}

/// Проверяет прежнее правило последнего корректного повторного параметра.
TEST(CliArguments, UsesLastRepeatedOption)
{
  const ParseResult parsed = parse({"cli", "solve", "--input", "first.json", "--input", "second.json", "--output",
                                    "first-out.json", "--output", "second-out.json", "--seed", "1", "--seed", "2"});
  ASSERT_TRUE(parsed.command.has_value());
  const auto & command = std::get<SolveCommand>(*parsed.command);
  EXPECT_EQ(command.inputPath, "second.json");
  EXPECT_EQ(command.outputPath, "second-out.json");
  EXPECT_EQ(command.seed, 2U);
}

/// Проверяет разбор команды валидации и условную доступность проверки модели.
TEST(CliArguments, ParsesValidationCommands)
{
  const ParseResult validation = parse({"cli", "validate", "--solution", "s.json", "--problem", "p.json"});
  ASSERT_TRUE(validation.command.has_value());
  const auto & command = std::get<ValidateCommand>(*validation.command);
  EXPECT_EQ(command.problemPath, "p.json");
  EXPECT_EQ(command.solutionPath, "s.json");

  const ParseResult model = parse({"cli", "validate-model", "--input", "model"});
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
  ASSERT_TRUE(model.command.has_value());
  EXPECT_EQ(std::get<ValidateModelCommand>(*model.command).inputPath, "model");
#else
  EXPECT_FALSE(model.command.has_value());
  EXPECT_TRUE(model.showUsage);
#endif
}

/// Проверяет отказ на отсутствующих значениях, неизвестных параметрах и неверных бюджетах.
TEST(CliArguments, RejectsInvalidSyntaxAndRanges)
{
  EXPECT_FALSE(parse({"cli", "solve", "--input"}).command.has_value());
  EXPECT_FALSE(parse({"cli", "solve", "--input", "in", "--output", "out", "--beam-width", "0"}).command.has_value());
  EXPECT_FALSE(parse({"cli", "solve", "--input", "in", "--output", "out", "--seed", "18446744073709551616"}).command.has_value());
  const ParseResult unknown = parse({"cli", "solve", "--input", "in", "--output", "out", "--unknown", "1"});
  EXPECT_FALSE(unknown.command.has_value());
  EXPECT_EQ(unknown.diagnostic, "Неизвестный параметр: --unknown\n");
  EXPECT_TRUE(unknown.showUsage);
}

/// Проверяет прежние код и диагностику для неизвестной команды и отсутствующего файла.
TEST(CliCommands, PreservesFailureCodesAndMessages)
{
  std::ostringstream output;
  std::ostringstream diagnostics;
  EXPECT_EQ(invoke({"cli", "unknown"}, output, diagnostics), static_cast<int>(CliExitCode::Failure));
  EXPECT_NE(diagnostics.str().find("Использование:"), std::string::npos);

  output.str({});
  diagnostics.str({});
  EXPECT_EQ(invoke({"cli", "solve", "--input", "missing.json", "--output", "out.json"}, output, diagnostics),
            static_cast<int>(CliExitCode::Failure));
  EXPECT_EQ(diagnostics.str(), "Не удалось открыть входной файл задачи\n");
}
