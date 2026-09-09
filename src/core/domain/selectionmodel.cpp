#include <algorithm>

#include <algs.h>
#include <rect2d.h>
#include <selectionmodel.h>


//
void SelectionModel::add(std::shared_ptr<Figure> figure)
{
  figures_.emplace_back(std::move(figure));
}


//
void SelectionModel::remove(std::shared_ptr<Figure> figure)
{
  std::erase(figures_, std::move(figure));
}


//
void SelectionModel::clearFigures()
{
  figures_.clear();
}


//
void SelectionModel::forEachFigures(std::function<bool(std::shared_ptr<Figure>)> pred)
{
  for (std::shared_ptr<Figure> & figure : figures_)
  {
    if (!pred(figure))
      break;
  }
}


//
std::shared_ptr<Figure> SelectionModel::findFigure(const Point2D & point) const
{
  /*
    auto it = std::ranges::find_if(figures_,
        [&point](const std::shared_ptr<Figure>& figure)
        {
            return Figure({0,0});//figure->contains(point);
        });

    if (it != figures_.end())
        return *it;
    return nullptr;
    */

  return nullptr;
}


//
bool SelectionModel::findFigure(std::shared_ptr<Figure> figure)
{
  auto it = std::find(figures_.begin(), figures_.end(), std::move(figure));

  if (it != figures_.end())
    return true;
  return false;
}


//
bool SelectionModel::isEmptyFigures() const
{
  return figures_.empty();
}
