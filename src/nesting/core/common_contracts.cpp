#include <aipackaging/nesting/common_contracts.h>

namespace aipackaging::solver
{
/// Сопоставляет статус решения с неизменным строковым значением публичного контракта.
std::string toString(const SolveStatus status)
{
  switch (status)
  {
    case SolveStatus::Solved:
      return "solved";
    case SolveStatus::NoSolutionFound:
      return "no_solution_found";
    case SolveStatus::BudgetExhausted:
      return "budget_exhausted";
    case SolveStatus::TimedOut:
      return "timed_out";
    case SolveStatus::UnsupportedEnvironment:
      return "unsupported_environment";
    case SolveStatus::InvalidProblem:
      return "invalid_problem";
  }
  return "invalid_problem";
}

/// Сопоставляет семейство решателя с неизменным строковым значением публичного контракта.
std::string toString(const SolverFamily family)
{
  switch (family)
  {
    case SolverFamily::Baseline:
      return "baseline";
    case SolverFamily::Neural:
      return "neural";
    case SolverFamily::Hybrid:
      return "hybrid";
  }
  return "baseline";
}

/// Сравнивает строку со значениями статуса формата обмена и записывает перечисление.
bool parseSolveStatus(const std::string & value, SolveStatus & status)
{
  if (value == "solved")
    status = SolveStatus::Solved;
  else if (value == "no_solution_found")
    status = SolveStatus::NoSolutionFound;
  else if (value == "budget_exhausted")
    status = SolveStatus::BudgetExhausted;
  else if (value == "timed_out")
    status = SolveStatus::TimedOut;
  else if (value == "unsupported_environment")
    status = SolveStatus::UnsupportedEnvironment;
  else if (value == "invalid_problem")
    status = SolveStatus::InvalidProblem;
  else
    return false;
  return true;
}

/// Сравнивает строку со значениями семейства формата обмена и записывает перечисление.
bool parseSolverFamily(const std::string & value, SolverFamily & family)
{
  if (value == "baseline")
    family = SolverFamily::Baseline;
  else if (value == "neural")
    family = SolverFamily::Neural;
  else if (value == "hybrid")
    family = SolverFamily::Hybrid;
  else
    return false;
  return true;
}
} // namespace aipackaging::solver
