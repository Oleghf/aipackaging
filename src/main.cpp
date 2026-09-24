#include <QApplication>

#include <polygondesktopinfrastructure.h>
#include <polygonmainwindow.h>
#include <polygonworkspacecontroller.h>
#include <qtapplicationdispatcher.h>

/// Создаёт полигональное окно и его адаптеры в порядке безопасного завершения фоновой работы.
int main(int argc, char * argv[])
{
  QApplication app(argc, argv);
  QApplication::setOrganizationName(QStringLiteral("AIPackaging"));
  QApplication::setApplicationName(QStringLiteral("AIPackaging"));

  PolygonMainWindow mainWindow;
  mainWindow.show();

  // Порядок объявления удерживает окно дольше контроллера и фонового средства запуска.
  auto polygonOutput = std::shared_ptr<IPolygonWorkspaceOutput>(&mainWindow, [](IPolygonWorkspaceOutput *) {});
  auto polygonStore = std::make_shared<PolygonArtifactStore>();
  auto polygonDocuments = std::make_shared<LocalPolygonDocumentGateway>(polygonStore);
  auto baselineBackend = std::make_shared<BaselinePolygonBackend>(polygonStore);
  auto polygonDispatcher = std::make_shared<QtApplicationDispatcher>(&mainWindow);
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
  auto polygonModelGateway = std::make_shared<LocalPolygonModelGateway>(polygonStore);
  auto polygonModels = std::make_shared<StdThreadPolygonModelJobRunner>(polygonModelGateway, polygonDispatcher);
  auto neuralBackend = std::make_shared<OnnxPolygonBackend>(polygonStore);
  auto polygonBackend = std::make_shared<PolygonBackendRouter>(baselineBackend, neuralBackend);
#else
  std::shared_ptr<IPolygonModelJobRunner> polygonModels;
  auto polygonBackend = std::make_shared<PolygonBackendRouter>(baselineBackend);
#endif
  auto polygonJobs = std::make_shared<StdThreadNestingJobRunner>(polygonBackend, polygonDispatcher);
  auto polygonController =
    std::make_shared<PolygonWorkspaceController>(polygonOutput, polygonDocuments, polygonJobs, polygonModels);
  mainWindow.setPolygonWorkspaceActions(polygonController->actions());

  return app.exec();
}
