#ifndef PREVIEWPOOLPRESENTER_H__
#define PREVIEWPOOLPRESENTER_H__

#include <memory>

class Board;
class Figure;

////////////////////////////////////////////////////////////////////////////////
//
/// Представление фигуры предпросмотра пула на доске генерации
/**
*/
////////////////////////////////////////////////////////////////////////////////
class PreviewPoolPresenter
{
public:
  // Конструктор
  explicit PreviewPoolPresenter(std::shared_ptr<Board> board);

  // Показать фигуру предпросмотра на доске генерации
  void showPreview(const std::shared_ptr<Figure> & figure) const;
  // Очистить доску генерации
  void clear() const;

private:
  std::shared_ptr<Board> board_;
};

#endif
