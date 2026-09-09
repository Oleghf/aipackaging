#include <algs.h>
#include <board.h>
#include <packing/placementvalidator.h>

//------------------------------------------------------------------------------
/**
  Проверяет возможность размещения
*/
//--
bool PlacementValidationResult::canPlace() const
{
  return code == PlacementValidationCode::Ok;
}


//------------------------------------------------------------------------------
/**
  Возвращает сообщение результата проверки
*/
//--
std::string PlacementValidationResult::message() const
{
  switch (code)
  {
    case PlacementValidationCode::Ok:
      return "Размещение возможно";
    case PlacementValidationCode::InvalidBoard:
      return "Доска упаковки недоступна";
    case PlacementValidationCode::InvalidFigure:
      return "Фигура недоступна";
    case PlacementValidationCode::EmptyPool:
      return "Пул фигур пуст";
    case PlacementValidationCode::EmptyFigure:
      return "Фигура не содержит клеток";
    case PlacementValidationCode::OutOfBounds:
      return "Фигура выходит за границы доски";
    case PlacementValidationCode::Intersection:
      return "Фигура пересекается с уже размещённой фигурой";
    case PlacementValidationCode::NoPlacementFound:
      return "Не найдено место для автоматического размещения фигуры";
  }

  return "Неизвестный результат проверки размещения";
}


//------------------------------------------------------------------------------
/**
  Проверяет границы доски и пересечения с уже размещенными фигурами
*/
//--
PlacementValidationResult PlacementValidator::validate(const std::shared_ptr<Board> & board,
                                                       const std::shared_ptr<Figure> & figure) const
{
  if (!board)
    return {PlacementValidationCode::InvalidBoard};

  if (!figure)
    return {PlacementValidationCode::InvalidFigure};

  if (figure->GetCells().empty())
    return {PlacementValidationCode::EmptyFigure};

  if (!BoundingBoxContains(*board, *figure))
    return {PlacementValidationCode::OutOfBounds};

  for (const std::shared_ptr<Figure> & placedFigure : board->getFigures())
  {
    if (placedFigure == figure)
      continue;

    if (!placedFigure || placedFigure->GetCells().empty())
      continue;

    if (FigureIntersect(*placedFigure, *figure))
      return {PlacementValidationCode::Intersection};
  }

  return {PlacementValidationCode::Ok};
}


//------------------------------------------------------------------------------
/**
  Проверяет возможность размещения фигуры на доске
*/
//--
bool PlacementValidator::canPlace(const std::shared_ptr<Board> & board, const std::shared_ptr<Figure> & figure) const
{
  return validate(board, figure).canPlace();
}
