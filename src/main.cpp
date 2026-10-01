#include <QApplication>
#include <QDir>
#include <QStandardPaths>

#include <nesting_job_runner.h>
#include <polygon_artifact_store.h>
#include <polygon_backends.h>
#include <polygon_document_gateway.h>
#include <polygon_draft_jobs.h>
#include <polygon_editable_document_gateway.h>
#include <polygon_import_jobs.h>
#include <polygon_model_jobs.h>
#include <polygondocumentcontroller.h>
#include <polygonimportcontroller.h>
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
  auto importOutput = std::shared_ptr<IPolygonImportOutput>(&mainWindow, [](IPolygonImportOutput *) {});
  auto polygonStore = std::make_shared<PolygonArtifactStore>();
  auto polygonDocuments = std::make_shared<LocalPolygonDocumentGateway>(polygonStore);
  auto editableDocuments = std::make_shared<LocalPolygonEditableDocumentGateway>(polygonStore);
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
  auto polygonJobs = std::make_shared<StdThreadNestingJobRunner>(polygonBackend, polygonDispatcher, polygonDocuments);
  auto importGateway = std::make_shared<LocalPolygonImportGateway>(editableDocuments, polygonDocuments);
  auto importJobs = std::make_shared<StdThreadPolygonImportJobRunner>(importGateway, polygonDispatcher);
  auto draftJobs = std::make_shared<StdThreadPolygonDraftJobRunner>(editableDocuments, polygonDispatcher);
  auto activeDocument = std::make_shared<ActivePolygonDocument>();
  auto polygonController =
    std::make_shared<PolygonWorkspaceController>(polygonOutput, polygonDocuments, polygonJobs, polygonModels, activeDocument);
  const QString autosaveDirectory =
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("autosave"));
  QDir().mkpath(autosaveDirectory);
  const std::string autosavePath = QDir(autosaveDirectory).filePath(QStringLiteral("active.aipdraft.json")).toStdString();
  auto documentController =
    std::make_shared<PolygonDocumentController>(editableDocuments, polygonController, activeDocument, autosavePath, draftJobs);
  auto importController = std::make_shared<PolygonImportController>(importOutput, importJobs, documentController);
  PolygonWorkspaceActions actions = polygonController->actions();
  documentController->bindActions(actions);
  mainWindow.setPolygonWorkspaceActions(std::move(actions));
  mainWindow.setPolygonImportActions(importController->actions());
  documentController->inspectRecovery();

  return app.exec();
}
