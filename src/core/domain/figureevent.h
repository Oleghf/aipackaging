#ifndef FIGUREEVENT_H__
#define FIGUREEVENT_H__

enum class FigureEventType
{
  FigureMoved,
  FigureRotated
};

////////////////////////////////////////////////////////////////////////////////
//
/// Базовый класс события фигуры
/**
*/
////////////////////////////////////////////////////////////////////////////////
class FigureEvent
{
public:
  FigureEvent(size_t figureID)
    : figureID_(figureID)
  {
  }
  virtual ~FigureEvent() = default;

  inline size_t figureID() const { return figureID_; }
  virtual FigureEventType type() const = 0;

private:
  const size_t figureID_;
};

#endif
