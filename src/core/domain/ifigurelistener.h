#ifndef IFIGURELISTENER_H_
#define IFIGURELISTENER_H_

class FigureEvent;

////////////////////////////////////////////////////////////////////////////////
//
/// Наблюдатель фигуры
/** \details Позволяет получать доменные события фигуры
*/
////////////////////////////////////////////////////////////////////////////////
class IFigureListener
{
public:
  virtual ~IFigureListener() = default;

  virtual void onFigureEvent(const FigureEvent & event) = 0;
};

#endif
