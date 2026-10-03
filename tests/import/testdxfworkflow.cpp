#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <polygon_artifact_store.h>
#include <polygon_document_gateway.h>
#include <polygon_editable_document_gateway.h>
#include <polygon_import_jobs.h>
#include <polygonimportcontroller.h>

namespace
{
/// Записывает минимальную замкнутую деталь во временный ASCII DXF.
std::filesystem::path writeDxf()
{
  const auto path = std::filesystem::temp_directory_path() / "aipackaging-m7-import.dxf";
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n"
            "0\nSECTION\n2\nENTITIES\n"
            "0\nLWPOLYLINE\n8\nCUT\n90\n4\n70\n1\n"
            "10\n0\n20\n0\n10\n20\n20\n0\n10\n20\n20\n10\n10\n0\n20\n10\n"
            "0\nENDSEC\n0\nEOF\n";
  return path;
}

/// Отклоняет каждое событие и считает обращения рабочего потока.
class RejectingDispatcher final : public IApplicationDispatcher
{
public:
  /// Возвращает отказ без синхронного выполнения функции.
  bool post(std::function<void()>) noexcept override
  {
    ++attempts;
    return false;
  }

  std::atomic<std::size_t> attempts = 0;
};

/// Возвращает управляемые результаты и считает освобождённые ресурсы.
class ImportGatewayStub final : public IPolygonImportGateway
{
public:
  /// Возвращает один зарегистрированный сеанс.
  PolygonImportInspection inspect(const PolygonImportInspectionRequest & request) override
  {
    PolygonImportInspection result;
    result.success = true;
    result.session = {7};
    result.sourceIdentifier = request.filePath;
    return result;
  }

  /// Возвращает документ с имитацией точного процессного снимка.
  PolygonImportBuildResult build(PolygonImportSessionHandle, const PolygonImportConfiguration &) override
  {
    PolygonImportBuildResult result;
    result.success = true;
    result.document.success = true;
    result.document.compiled = PolygonDocumentLoadResult{true, {}, {9}};
    return result;
  }

  /// Учитывает освобождение сеанса.
  void release(PolygonImportSessionHandle) noexcept override { ++releasedSessions; }
  /// Учитывает освобождение результата.
  void release(const PolygonImportBuildResult &) noexcept override { ++releasedResults; }

  std::atomic<std::size_t> releasedSessions = 0;
  std::atomic<std::size_t> releasedResults = 0;
};

/// Сохраняет последний снимок мастера для проверки переходов контроллера.
class ImportOutputStub final : public IPolygonImportOutput
{
public:
  /// Принимает снимок без дополнительной обработки.
  void presentPolygonImport(const PolygonImportSnapshot & value) override
  {
    snapshot = value;
    ++publications;
  }

  PolygonImportSnapshot snapshot;
  std::size_t publications = 0;
};

/// Позволяет тесту управляемо завершать и отменять прикладные работы.
class ImportJobRunnerStub final : public IPolygonImportJobRunner
{
public:
  /// Сохраняет функции анализа и возвращает новый идентификатор.
  std::optional<PolygonImportJobHandle> inspect(const PolygonImportInspectionRequest &, PolygonImportJobCallbacks value,
                                                std::string &) override
  {
    callbacks = std::move(value);
    active = PolygonImportJobHandle{nextJob++};
    return active;
  }

  /// Сохраняет функции построения и возвращает новый идентификатор.
  std::optional<PolygonImportJobHandle> build(PolygonImportSessionHandle, const PolygonImportConfiguration &,
                                              PolygonImportJobCallbacks value, std::string &) override
  {
    callbacks = std::move(value);
    active = PolygonImportJobHandle{nextJob++};
    return active;
  }

  /// Учитывает запрос отмены совпадающей работы.
  void cancel(PolygonImportJobHandle job) noexcept override { cancelled = job; }

  /// Учитывает освобождение сеанса контроллером.
  void release(PolygonImportSessionHandle) noexcept override { ++releasedSessions; }
  /// Учитывает освобождение результата контроллером.
  void release(const PolygonImportBuildResult &) noexcept override { ++releasedResults; }

  std::uint64_t nextJob = 1;
  std::optional<PolygonImportJobHandle> active;
  std::optional<PolygonImportJobHandle> cancelled;
  PolygonImportJobCallbacks callbacks;
  std::size_t releasedSessions = 0;
  std::size_t releasedResults = 0;
};

/// Ожидает выполнения условия рабочим потоком с ограниченным сроком.
bool waitFor(const std::function<bool()> & condition)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline)
  {
    if (condition())
      return true;
    std::this_thread::yield();
  }
  return condition();
}
} // namespace

/// Проверяет полный путь от файла DXF до точного зарегистрированного снимка задачи.
TEST(DxfImportWorkflow, CompilesImportedDocumentThroughExistingValidator)
{
  const auto store = std::make_shared<PolygonArtifactStore>();
  const auto documents = std::make_shared<LocalPolygonDocumentGateway>(store);
  const auto editable = std::make_shared<LocalPolygonEditableDocumentGateway>(store);
  LocalPolygonImportGateway gateway(editable, documents);
  const auto original = writeDxf();
  const auto path = original.parent_path() / std::filesystem::path(u8"деталь 漢字 с пробелом.dxf");
  std::filesystem::rename(original, path);
  const auto encoded = path.u8string();
  const std::string source(encoded.begin(), encoded.end());
  const PolygonImportInspection inspection = gateway.inspect({source});
  ASSERT_TRUE(inspection.success) << inspection.error;
  ASSERT_TRUE(inspection.session);
  ASSERT_EQ(inspection.paths.size(), 1U);

  PolygonImportConfiguration configuration;
  configuration.unit = PolygonImportUnit::Millimeter;
  configuration.problemId = "imported-dxf";
  configuration.sheetWidth = 30.0;
  configuration.sheetHeight = 20.0;
  const PolygonImportBuildResult result = gateway.build(inspection.session, configuration);
  ASSERT_TRUE(result.success) << result.error;
  ASSERT_TRUE(result.document.compiled.has_value());
  EXPECT_EQ(result.document.source, PolygonDocumentSource::Imported);
  EXPECT_EQ(result.document.sourceIdentifier, source);
  gateway.release(result);
  gateway.release(inspection.session);
  std::filesystem::remove(path);
}

/// Проверяет освобождение сеанса, если диспетчер отказался принять итоговое событие.
TEST(DxfImportWorkflow, ReleasesInspectionWhenDeliveryIsRejected)
{
  const auto gateway = std::make_shared<ImportGatewayStub>();
  const auto dispatcher = std::make_shared<RejectingDispatcher>();
  StdThreadPolygonImportJobRunner runner(gateway, dispatcher);
  PolygonImportJobCallbacks callbacks;
  callbacks.inspected = [](PolygonImportJobHandle, PolygonImportInspection)
  {
    return true;
  };
  std::string error;
  ASSERT_TRUE(runner.inspect({"part.dxf"}, std::move(callbacks), error));
  ASSERT_TRUE(waitFor([&]() { return dispatcher->attempts.load() == 1; }));
  EXPECT_TRUE(waitFor([&]() { return gateway->releasedSessions.load() == 1; }));

  PolygonImportJobCallbacks retry;
  retry.inspected = [](PolygonImportJobHandle, PolygonImportInspection)
  {
    return true;
  };
  EXPECT_TRUE(runner.inspect({"retry.dxf"}, std::move(retry), error));
}

/// Проверяет освобождение точного результата при отказе очереди событий.
TEST(DxfImportWorkflow, ReleasesBuildResultWhenDeliveryIsRejected)
{
  const auto gateway = std::make_shared<ImportGatewayStub>();
  const auto dispatcher = std::make_shared<RejectingDispatcher>();
  StdThreadPolygonImportJobRunner runner(gateway, dispatcher);
  PolygonImportJobCallbacks callbacks;
  callbacks.completed = [](PolygonImportJobHandle, PolygonImportBuildResult)
  {
    return true;
  };
  std::string error;
  ASSERT_TRUE(runner.build({7}, {}, std::move(callbacks), error));
  ASSERT_TRUE(waitFor([&]() { return dispatcher->attempts.load() == 1; }));
  EXPECT_TRUE(waitFor([&]() { return gateway->releasedResults.load() == 1; }));
}

/// Проверяет фильтрацию устаревших событий, отмену и освобождение временного сеанса.
TEST(DxfImportWorkflow, ControllerOwnsOnlyCurrentImportSession)
{
  const auto output = std::make_shared<ImportOutputStub>();
  const auto jobs = std::make_shared<ImportJobRunnerStub>();
  const auto controller = std::make_shared<PolygonImportController>(output, jobs, nullptr);
  PolygonImportActions actions = controller->actions();
  actions.inspect({"first.dxf"});
  ASSERT_EQ(output->snapshot.state, PolygonImportState::Inspecting);
  ASSERT_TRUE(jobs->active.has_value());
  const PolygonImportJobHandle firstJob = *jobs->active;

  PolygonImportInspection inspection;
  inspection.success = true;
  inspection.session = {41};
  inspection.sourceIdentifier = "first.dxf";
  const auto staleInspectionCallback = jobs->callbacks.inspected;
  ASSERT_TRUE(staleInspectionCallback(firstJob, inspection));
  EXPECT_EQ(output->snapshot.state, PolygonImportState::Ready);

  actions.build({});
  ASSERT_TRUE(jobs->active.has_value());
  const PolygonImportJobHandle buildJob = *jobs->active;
  PolygonImportInspection stale;
  stale.success = true;
  stale.session = {99};
  EXPECT_FALSE(staleInspectionCallback(firstJob, std::move(stale)));
  EXPECT_EQ(output->snapshot.state, PolygonImportState::Building);

  actions.cancel();
  EXPECT_EQ(jobs->cancelled, buildJob);
  jobs->callbacks.cancelled(buildJob);
  EXPECT_EQ(output->snapshot.state, PolygonImportState::Ready);
  actions.close();
  EXPECT_EQ(jobs->releasedSessions, 1U);
  EXPECT_EQ(output->snapshot.state, PolygonImportState::Empty);
}
