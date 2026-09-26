#include <fstream>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <aipackaging/nesting/grid_environment.h>
#include <aipackaging/nesting/grid_io.h>
#include <aipackaging/nesting/grid_solver.h>
#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_io.h>
#include <aipackaging/nesting/polygon_solver.h>
#include <cli_arguments.h>
#include <cli_commands.h>
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
#include <aipackaging/inference/polygon_onnx.h>
#endif

namespace aipackaging::cli
{
namespace
{
using namespace solver;

/// Читает файл целиком для определения формата обмена до предметного разбора.
bool readTextFile(const std::string & path, std::string & text)
{
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  return static_cast<bool>(input) || input.eof();
}

/// Преобразует проверенные параметры CLI в контракт базового решателя.
SolverConfig solverConfig(const SolveCommand & command)
{
  SolverConfig config;
  config.seed = command.seed;
  config.randomIterations = command.randomIterations;
  config.beamWidth = command.beamWidth;
  config.maxExpandedStates = command.maxExpandedStates;
  config.timeoutMs = command.timeoutMs;
  parseSolverKind(command.solverName, config.solver);
  return config;
}

/// Проверяет, что имя обозначает один из нейросетевых режимов полигонального решателя.
bool isNeuralSolver(std::string_view solverName)
{
  return solverName == "neural-greedy" || solverName == "neural-best-of" || solverName == "hybrid";
}
} // namespace

/// Выводит прежнюю справку без изменения строк и порядка параметров.
void printUsage(std::ostream & diagnostics)
{
  diagnostics << "Использование: `AIPackaging_Cli solve --input <problem.json> --output <solution.json>`\n"
                 "       `[--solver input-first-fit|area-left-bottom|max-side-left-bottom|random-left-bottom|beam]`\n"
                 "       `[--seed N] [--random-iterations N] [--beam-width N]`\n"
                 "       `[--max-expanded-states N] [--timeout-ms N]`\n"
                 "       `[--model <каталог>] [--rollouts N]`\n"
                 "       `AIPackaging_Cli validate --problem <problem.json> --solution <solution.json>`\n"
                 "       `AIPackaging_Cli validate-model --input <каталог>`\n";
}

/// Загружает оба документа, выбирает валидатор по формату задачи и сохраняет прежнюю классификацию ошибок.
CliExitCode runValidate(const ValidateCommand & command, std::ostream & diagnostics)
{
  std::string problemText;
  std::string solutionText;
  if (command.problemPath.empty() || command.solutionPath.empty() || !readTextFile(command.problemPath, problemText) ||
      !readTextFile(command.solutionPath, solutionText))
  {
    diagnostics << "Не удалось прочитать входные данные проверки\n";
    return CliExitCode::Failure;
  }
  const std::string format = detectJsonFormat(problemText);
  if (format == "aipackaging.grid_problem")
  {
    const GridProblemLoadResult problem = loadGridProblemFromText(problemText);
    const GridSolutionLoadResult solution = loadGridSolutionFromText(solutionText);
    const ValidationResult result = problem.success && solution.success
                                    ? validateGridSolution(problem.problem, solution.solution)
                                    : ValidationResult{false, problem.success ? solution.error : problem.error};
    if (!result.success)
      diagnostics << result.error << '\n';
    return result.success ? CliExitCode::Success : CliExitCode::InvalidData;
  }
  if (format == "aipackaging.polygon_problem")
  {
    const PolygonProblemLoadResult problem = loadPolygonProblemFromText(problemText);
    const PolygonSolutionLoadResult solution = loadPolygonSolutionFromText(solutionText);
    const ValidationResult result = problem.success && solution.success
                                    ? validatePolygonSolution(problem.problem, solution.solution)
                                    : ValidationResult{false, problem.success ? solution.error : problem.error};
    if (!result.success)
      diagnostics << result.error << '\n';
    return result.success ? CliExitCode::Success : CliExitCode::InvalidData;
  }
  diagnostics << "Неподдерживаемый формат задачи\n";
  return CliExitCode::InvalidData;
}

/// Загружает комплект ONNX в поддерживающей его сборке и печатает проверенную идентичность модели.
CliExitCode runValidateModel(const ValidateModelCommand & command, std::ostream & output, std::ostream & diagnostics)
{
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
  std::string error;
  const auto model = inference::PolygonOnnxPolicy::Load(command.inputPath, error);
  if (!model)
  {
    diagnostics << error << '\n';
    return CliExitCode::InvalidData;
  }
  output << model->metadata().modelId << ' ' << model->metadata().modelSha256 << '\n';
  return CliExitCode::Success;
#else
  static_cast<void>(command);
  static_cast<void>(output);
  static_cast<void>(diagnostics);
  return CliExitCode::Failure;
#endif
}

/// Загружает задачу, запускает выбранную реализацию и атомарно сохраняет полное или частичное решение.
CliExitCode runSolve(const SolveCommand & command, std::ostream & diagnostics)
{
  std::string inputText;
  if (!readTextFile(command.inputPath, inputText))
  {
    diagnostics << "Не удалось открыть входной файл задачи\n";
    return CliExitCode::Failure;
  }
  const std::string format = detectJsonFormat(inputText);
  SolverConfig config = solverConfig(command);
  if (format == "aipackaging.polygon_problem")
  {
    const PolygonProblemLoadResult loaded = loadPolygonProblemFromText(inputText);
    if (!loaded.success)
    {
      diagnostics << loaded.error << '\n';
      return CliExitCode::InvalidData;
    }
    PolygonSolution solution;
    if (isNeuralSolver(command.solverName))
    {
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
      if (command.modelPath.empty())
      {
        diagnostics << "Для нейросетевого решателя требуется путь к модели\n";
        return CliExitCode::Failure;
      }
      std::string modelError;
      const auto model = inference::PolygonOnnxPolicy::Load(command.modelPath, modelError);
      if (!model)
      {
        diagnostics << modelError << '\n';
        return CliExitCode::InvalidData;
      }
      inference::PolygonPolicyConfig policy;
      policy.mode =
        command.solverName == "neural-greedy" ? inference::PolygonPolicyMode::Greedy : inference::PolygonPolicyMode::BestOf;
      policy.seed = config.seed;
      policy.rollouts = command.neuralRollouts;
      policy.timeoutMs = config.timeoutMs;
      if (command.solverName == "hybrid")
      {
        SolverConfig fallback = config;
        fallback.solver = SolverKind::RandomLeftBottom;
        fallback.randomIterations = 64;
        const auto result = model->runHybrid(loaded.problem, policy, fallback);
        if (result.fallbackUsed)
          diagnostics << result.warning << '\n';
        solution = result.solution;
      }
      else
        solution = model->run(loaded.problem, policy).solution;
#else
      diagnostics << "Эта сборка не содержит внутреннюю реализацию ONNX\n";
      return CliExitCode::Failure;
#endif
    }
    else
      solution = solvePolygonProblem(loaded.problem, config);
    std::string outputError;
    if (!savePolygonSolutionToFile(command.outputPath, solution, outputError))
    {
      diagnostics << outputError << '\n';
      return CliExitCode::Failure;
    }
    return solution.complete() ? CliExitCode::Success : CliExitCode::PartialSolution;
  }
  if (format != "aipackaging.grid_problem")
  {
    diagnostics << "Формат задачи отсутствует или не поддерживается\n";
    return CliExitCode::InvalidData;
  }
  if (isNeuralSolver(command.solverName))
  {
    diagnostics << "Нейросетевые режимы доступны только для полигональных задач\n";
    return CliExitCode::Failure;
  }
  const GridProblemLoadResult loaded = loadGridProblemFromText(inputText);
  if (!loaded.success)
  {
    diagnostics << loaded.error << '\n';
    return CliExitCode::InvalidData;
  }
  const GridSolution solution = solveGridProblem(loaded.problem, config);
  std::string outputError;
  if (!saveGridSolutionToFile(command.outputPath, solution, outputError))
  {
    diagnostics << outputError << '\n';
    return CliExitCode::Failure;
  }
  return solution.complete() ? CliExitCode::Success : CliExitCode::PartialSolution;
}

/// Преобразует массив процесса в представления строк, разбирает команду и вызывает соответствующий обработчик.
int runCli(int argc, char ** argv, std::ostream & output, std::ostream & diagnostics)
{
  std::vector<std::string_view> arguments;
  arguments.reserve(static_cast<std::size_t>(argc));
  for (int index = 0; index < argc; ++index)
    arguments.emplace_back(argv[index]);
  const ParseResult parsed = parseArguments(arguments);
  if (!parsed.command)
  {
    diagnostics << parsed.diagnostic;
    if (parsed.showUsage)
      printUsage(diagnostics);
    return static_cast<int>(CliExitCode::Failure);
  }
  const CliExitCode result = std::visit(
    [&output, &diagnostics](const auto & command) -> CliExitCode
    {
      using Command = std::decay_t<decltype(command)>;
      if constexpr (std::is_same_v<Command, SolveCommand>)
        return runSolve(command, diagnostics);
      else if constexpr (std::is_same_v<Command, ValidateCommand>)
        return runValidate(command, diagnostics);
      else
        return runValidateModel(command, output, diagnostics);
    },
    *parsed.command);
  return static_cast<int>(result);
}
} // namespace aipackaging::cli
