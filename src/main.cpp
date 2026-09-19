#include <QApplication>

#include <polygondesktopinfrastructure.h>
#include <polygonmainwindow.h>
#include <polygonworkspacecontroller.h>
#include <qtapplicationdispatcher.h>

/// Создаёт полигональное окно и его адаптеры в порядке безопасного завершения фоновой работы.
int main(int argc, char * argv[])
{
  QApplication app(argc, argv);

  PolygonMainWindow mainWindow;
  mainWindow.show();

  // Порядок объявления удерживает окно дольше контроллера и фонового средства запуска.
  auto polygonOutput = std::shared_ptr<IPolygonWorkspaceOutput>(&mainWindow, [](IPolygonWorkspaceOutput *) {});
  auto polygonStore = std::make_shared<PolygonArtifactStore>();
  auto polygonDocuments = std::make_shared<LocalPolygonDocumentGateway>(polygonStore);
  auto baselineBackend = std::make_shared<BaselinePolygonBackend>(polygonStore);
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
  auto polygonModels = std::make_shared<LocalPolygonModelGateway>(polygonStore);
  auto neuralBackend = std::make_shared<OnnxPolygonBackend>(polygonStore);
  auto polygonBackend = std::make_shared<PolygonBackendRouter>(baselineBackend, neuralBackend);
#else
  std::shared_ptr<IPolygonModelGateway> polygonModels;
  auto polygonBackend = std::make_shared<PolygonBackendRouter>(baselineBackend);
#endif
  auto polygonDispatcher = std::make_shared<QtApplicationDispatcher>(&mainWindow);
  auto polygonJobs = std::make_shared<StdThreadNestingJobRunner>(polygonBackend, polygonDispatcher);
  auto polygonController =
    std::make_shared<PolygonWorkspaceController>(polygonOutput, polygonDocuments, polygonJobs, polygonModels);
  mainWindow.setPolygonWorkspaceActions(polygonController->actions());

  return app.exec();
}
