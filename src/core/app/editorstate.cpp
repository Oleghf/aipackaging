#include <editorstate.h>


//
EditorState::EditorState()
{
}


//
std::unique_ptr<ICommand> EditorState::onEvent(const Event & event)
{
  return nullptr;
}
