#ifndef MAINCONTROLLER_H__
#define MAINCONTROLLER_H__

#include <eventlistener.h>

class IState;
class CommandManager;
class DrawController;
class PackingController;
class SelectionModel;
class Board;
class IView;
class IFileDialogView;
class IRedrawView;
class IStatisticsView;
class IPolygonWorkspaceView;
class PolygonWorkspaceController;
enum class PackingMode;
namespace ApplicationStateService
{
class IStateSession;
}

////////////////////////////////////////////////////////////////////////////////
//
/// Контроллер
/**
*/
////////////////////////////////////////////////////////////////////////////////
class MainController : public EventListener
{
public:
  MainController(std::shared_ptr<IView> view);

  void onEvent(const Event & event);
  void setPackingMode(PackingMode mode);

private:
  void save(const std::string & filePath);

  void openScene(const std::string & filePath);

  void load(const std::string & filePath);

  void showPackingMessage(const std::string & title);

private:
  std::shared_ptr<IView> messageView_;
  std::shared_ptr<IFileDialogView> fileDialogView_;
  std::shared_ptr<IRedrawView> redrawView_;
  std::shared_ptr<IStatisticsView> statisticsView_;
  std::shared_ptr<ApplicationStateService::IStateSession> stateSession_;


  std::shared_ptr<SelectionModel> selection_;

  std::shared_ptr<CommandManager> commandManager_;

  std::shared_ptr<Board> mainBoard_;
  std::shared_ptr<Board> genBoard_;

  std::shared_ptr<DrawController> drawController_;
  std::shared_ptr<PackingController> packingController_;
  std::shared_ptr<IPolygonWorkspaceView> polygonWorkspaceView_;
  std::shared_ptr<PolygonWorkspaceController> polygonWorkspaceController_;
};

#endif
