#ifndef FIGUREPOOL_H__
#define FIGUREPOOL_H__

#include <memory>
#include <random>
#include <vector>

class Figure;

////////////////////////////////////////////////////////////////////////////////
//
/// Пул фигур для режима упаковки
/**
*/
////////////////////////////////////////////////////////////////////////////////
class FigurePool
{
public:
  struct State
  {
    std::vector<std::shared_ptr<Figure>> remaining;
    std::shared_ptr<Figure> preview;
  };

  // Конструктор
  FigurePool();
  // Конструктор с фиксированным зерном для тестов и детерминированной генерации
  explicit FigurePool(unsigned int seed);
  FigurePool(const FigurePool &) = default;
  FigurePool & operator=(const FigurePool &) = default;

  // Загрузить содержимое пула
  void load(std::vector<std::shared_ptr<Figure>> figures);
  // Очистить пул
  void clear();

  // Проверка на пустой пул
  bool isEmpty() const;
  // Есть ли фигура предпросмотра
  bool hasPreview() const;
  // Текущая фигура предпросмотра
  std::shared_ptr<Figure> previewFigure() const;
  // Забрать текущую фигуру предпросмотра
  std::shared_ptr<Figure> takePreview();
  // Вернуть фигуру как текущую фигуру предпросмотра
  void returnAsPreview(std::shared_ptr<Figure> figure);
  // Снять снимок оставшегося пула
  std::vector<std::shared_ptr<Figure>> snapshotRemaining() const;
  // Снять снимок внутреннего состояния пула
  State state() const;
  // Восстановить внутреннее состояние пула
  void restore(const State & state);

private:
  void promotePreview();

private:
  std::vector<std::shared_ptr<Figure>> remaining_;
  std::shared_ptr<Figure> preview_;
  std::mt19937 randomEngine_;
};

#endif
