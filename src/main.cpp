#include <QApplication>

#include <maincontroller.h>
#include <polygondesktopinfrastructure.h>
#include <polygonworkspacecontroller.h>
#include <qtapplicationdispatcher.h>
#include <qtview.h>

int main(int argc, char * argv[])
{
  QApplication app(argc, argv);

  QtView mainWindow;
  mainWindow.show();

  std::shared_ptr<IView> view(&mainWindow, [](IView *) {});
  std::shared_ptr<MainController> mainController = std::make_shared<MainController>(view);
  view->addEventListener(mainController);

  // Полигональный контур собирается отдельно от унаследованного клеточного контроллера.
  auto polygonOutput = std::shared_ptr<IPolygonWorkspaceOutput>(&mainWindow, [](IPolygonWorkspaceOutput *) {});
  auto polygonStore = std::make_shared<PolygonArtifactStore>();
  auto polygonDocuments = std::make_shared<LocalPolygonDocumentGateway>(polygonStore);
  auto polygonBackend = std::make_shared<BaselinePolygonBackend>(polygonStore);
  auto polygonDispatcher = std::make_shared<QtApplicationDispatcher>(&mainWindow);
  auto polygonJobs = std::make_shared<StdThreadNestingJobRunner>(polygonBackend, polygonDispatcher);
  auto polygonController = std::make_shared<PolygonWorkspaceController>(polygonOutput, polygonDocuments, polygonJobs);
  mainWindow.setPolygonWorkspaceActions(polygonController->actions());

  return app.exec();
}
