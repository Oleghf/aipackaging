#ifndef SELECTIONMODEL_H__
#define SELECTIONMODEL_H__

#include <functional>
#include <memory>

struct Point2D;
class Rect2D;
class Figure;

////////////////////////////////////////////////////////////////////////////////
//
/// Модель выделения
/**
*/
////////////////////////////////////////////////////////////////////////////////
class SelectionModel
{
public:
  // Добавить фигуру
  void add(std::shared_ptr<Figure> figure);

  // Удалить фигуру
  void remove(std::shared_ptr<Figure> figure);

  // Удалить все фигуры
  void clearFigures();

  // Применить предикат к каждой фигуре, пока предикат возвращает истину
  void forEachFigures(std::function<bool(std::shared_ptr<Figure>)> pred);

  // Найти фигуру
  std::shared_ptr<Figure> findFigure(const Point2D & point) const;
  bool findFigure(std::shared_ptr<Figure> figure);

  // Проверка на то есть ли фигуры на сцене
  bool isEmptyFigures() const;

private:
  std::vector<std::shared_ptr<Figure>> figures_;
};

#endif
