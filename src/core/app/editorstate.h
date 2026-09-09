#ifndef EDITORSTATE_H__
#define EDITORSTATE_H__

#include <istate.h>


////////////////////////////////////////////////////////////////////////////////
//
/// Состояние редактор фигур
/**
*/
////////////////////////////////////////////////////////////////////////////////
class EditorState : public IState
{
public:
  EditorState();

  std::unique_ptr<ICommand> onEvent(const Event & event) override;
};

#endif
