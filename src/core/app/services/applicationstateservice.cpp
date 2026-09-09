#include <applicationstateservice.h>
#include <editorstate.h>
#include <packagingstate.h>
#include <packingcontroller.h>

namespace ApplicationStateService
{
class Session final : public IStateSession
{
public:
  Session(std::shared_ptr<IState> packagingState, std::shared_ptr<IState> editorState)
    : currentState_(packagingState)
    , packagingState_(std::move(packagingState))
    , editorState_(std::move(editorState))
  {
  }

  std::shared_ptr<IState> current() const override { return currentState_; }

  void toggle() override
  {
    if (currentState_ == packagingState_)
      currentState_ = editorState_;
    else
      currentState_ = packagingState_;
  }

private:
  std::shared_ptr<IState> currentState_;
  std::shared_ptr<IState> packagingState_;
  std::shared_ptr<IState> editorState_;
};

std::shared_ptr<IStateSession> create(std::shared_ptr<PackingController> packingController)
{
  return std::make_shared<Session>(std::make_shared<PackagingState>(std::move(packingController)),
                                   std::make_shared<EditorState>());
}
} // namespace ApplicationStateService
