#ifndef PLACEMENTVALIDATOR_H__
#define PLACEMENTVALIDATOR_H__

#include <memory>
#include <string>

class Board;
class Figure;

////////////////////////////////////////////////////////////////////////////////
//
/// Код результата проверки размещения
/**
*/
////////////////////////////////////////////////////////////////////////////////
enum class PlacementValidationCode
{
  Ok,
  InvalidBoard,
  InvalidFigure,
  EmptyPool,
  EmptyFigure,
  OutOfBounds,
  Intersection,
  NoPlacementFound
};

////////////////////////////////////////////////////////////////////////////////
//
/// Результат проверки размещения
/**
*/
////////////////////////////////////////////////////////////////////////////////
struct PlacementValidationResult
{
  PlacementValidationCode code = PlacementValidationCode::Ok;

  // Проверить возможность размещения
  bool canPlace() const;
  // Получить диагностическое сообщение
  std::string message() const;
};

////////////////////////////////////////////////////////////////////////////////
//
/// Валидатор размещения фигуры на доске упаковки
/**
*/
////////////////////////////////////////////////////////////////////////////////
class PlacementValidator
{
public:
  // Проверить размещение фигуры на доске
  PlacementValidationResult validate(const std::shared_ptr<Board> & board, const std::shared_ptr<Figure> & figure) const;
  // Проверить возможность размещения фигуры на доске
  bool canPlace(const std::shared_ptr<Board> & board, const std::shared_ptr<Figure> & figure) const;
};

#endif
