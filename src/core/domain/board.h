#ifndef ___BOARD_H___
#define ___BOARD_H___

#include <functional>
#include <memory>
#include <vector>

#include <algs.h>
#include <ifigurelistener.h>
#include <line2d.h>

class IBoardListener;
class BoardEvent;
class PrimitiveView;
class Rect2D;

////////////////////////////////////////////////////////////////////////////////
//
/// Доска
/**
*/
////////////////////////////////////////////////////////////////////////////////
class Board : public std::enable_shared_from_this<Board>,
              public IFigureListener
{
public:
  static std::shared_ptr<Board> create(size_t countColumns, size_t countRows);

  inline size_t id() const { return id_; };

  inline size_t countColumns() const { return countColumns_; }
  inline size_t countRows() const { return countRows_; }

  Rect2D boundingBox(double cellSize) const;

  bool add(std::shared_ptr<Figure> figure);
  bool remove(std::shared_ptr<Figure> figure);

  void forEachFigures(const std::function<bool(std::shared_ptr<Figure>)> & pred);
  std::shared_ptr<Figure> findFigure(const Point2D & pos, double epsilon);

  // Если предикат возвращает истину, фигура добавляется в результат
  std::vector<std::shared_ptr<Figure>> getFigures(const std::function<bool(std::shared_ptr<Figure>)> & filter =
                                                    [](std::shared_ptr<Figure>) { return true; }) const;

  std::vector<Line2D> getPrimitivesGrid(double cellsize) const;

  void addEventListener(std::shared_ptr<IBoardListener> listener);
  void removeEventListener(std::shared_ptr<IBoardListener> listener);

  void onFigureEvent(const FigureEvent & event) override;

  bool operator==(const Board & rhs);
  bool operator!=(const Board & rhs);

private:
  Board(size_t countColumns, size_t countRows);
  void notifyListeners(const BoardEvent & event);

private:
  const size_t id_;
  const size_t countColumns_;
  const size_t countRows_;

  std::vector<std::shared_ptr<Figure>> figures_;
  std::vector<std::shared_ptr<IBoardListener>> listeners_;
};


#endif
