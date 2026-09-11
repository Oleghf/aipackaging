#include <aipackaging/nesting/search_contracts.h>

namespace aipackaging::solver
{
/// Преобразует перечисление алгоритма в закреплённое имя CLI и JSON-контракта.
std::string toString(SolverKind solver)
{
  switch (solver)
  {
    case SolverKind::InputFirstFit:
      return "input-first-fit";
    case SolverKind::AreaLeftBottom:
      return "area-left-bottom";
    case SolverKind::MaxSideLeftBottom:
      return "max-side-left-bottom";
    case SolverKind::RandomLeftBottom:
      return "random-left-bottom";
    case SolverKind::Beam:
      return "beam";
  }
  return "area-left-bottom";
}

/// Последовательно сравнивает строку со всеми допустимыми именами алгоритмов.
bool parseSolverKind(const std::string & value, SolverKind & solver)
{
  constexpr SolverKind VALUES[] = {SolverKind::InputFirstFit, SolverKind::AreaLeftBottom, SolverKind::MaxSideLeftBottom,
                                   SolverKind::RandomLeftBottom, SolverKind::Beam};
  for (const SolverKind candidate : VALUES)
  {
    if (toString(candidate) == value)
    {
      solver = candidate;
      return true;
    }
  }
  return false;
}
} // namespace aipackaging::solver
