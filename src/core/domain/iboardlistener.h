#ifndef IBOARDLISTENER_H_
#define IBOARDLISTENER_H_

class BoardEvent;

////////////////////////////////////////////////////////////////////////////////
//
/// Наблюдатель доски
/** \details Позволяет получать доменные события доски
*/
////////////////////////////////////////////////////////////////////////////////
class IBoardListener
{
public:
  virtual ~IBoardListener() = default;

  virtual void onBoardEvent(const BoardEvent & event) = 0;
};

#endif
