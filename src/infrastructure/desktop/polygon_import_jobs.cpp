#include "polygon_import_jobs.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

#include <aipackaging/import/dxf_import.h>

#include "../../support/utf8path.h"
#include "background_event_delivery.h"

using namespace aipackaging::desktop::detail;

namespace
{
/// Читает только обычный нессылочный файл и сохраняет его байты без преобразований.
bool readDxfFile(const std::string & filePath, std::string & contents, std::string & error)
{
  std::error_code statusError;
  const auto path = aipackaging::files::nativePath(filePath);
  const auto status = std::filesystem::symlink_status(path, statusError);
  if (statusError || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
  {
    error = "DXF должен быть обычным файлом, а не каталогом или символической ссылкой";
    return false;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input)
  {
    error = "Не удалось открыть DXF";
    return false;
  }
  contents.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  if (!input && !input.eof())
  {
    error = "Не удалось прочитать DXF";
    return false;
  }
  return true;
}

/// Сопоставляет прикладную единицу внутреннему контракту импортёра.
aipackaging::dxf::LengthUnit toDxfUnit(PolygonImportUnit unit) noexcept
{
  using DxfUnit = aipackaging::dxf::LengthUnit;
  switch (unit)
  {
    case PolygonImportUnit::Millimeter:
      return DxfUnit::Millimeter;
    case PolygonImportUnit::Centimeter:
      return DxfUnit::Centimeter;
    case PolygonImportUnit::Meter:
      return DxfUnit::Meter;
    case PolygonImportUnit::Kilometer:
      return DxfUnit::Kilometer;
    case PolygonImportUnit::Inch:
      return DxfUnit::Inch;
    case PolygonImportUnit::Foot:
      return DxfUnit::Foot;
    case PolygonImportUnit::Yard:
      return DxfUnit::Yard;
    case PolygonImportUnit::Micrometer:
      return DxfUnit::Micrometer;
    case PolygonImportUnit::Unknown:
      return DxfUnit::Unknown;
  }
  return DxfUnit::Unknown;
}

/// Сопоставляет внутреннюю единицу нейтральному прикладному перечислению.
PolygonImportUnit fromDxfUnit(aipackaging::dxf::LengthUnit unit) noexcept
{
  using DxfUnit = aipackaging::dxf::LengthUnit;
  switch (unit)
  {
    case DxfUnit::Millimeter:
      return PolygonImportUnit::Millimeter;
    case DxfUnit::Centimeter:
      return PolygonImportUnit::Centimeter;
    case DxfUnit::Meter:
      return PolygonImportUnit::Meter;
    case DxfUnit::Kilometer:
      return PolygonImportUnit::Kilometer;
    case DxfUnit::Inch:
      return PolygonImportUnit::Inch;
    case DxfUnit::Foot:
      return PolygonImportUnit::Foot;
    case DxfUnit::Yard:
      return PolygonImportUnit::Yard;
    case DxfUnit::Micrometer:
      return PolygonImportUnit::Micrometer;
    case DxfUnit::Unknown:
      return PolygonImportUnit::Unknown;
  }
  return PolygonImportUnit::Unknown;
}

/// Переносит диагностику импортёра без раскрытия его перечислений приложению.
PolygonImportDiagnostic toApplicationDiagnostic(const aipackaging::dxf::Diagnostic & source)
{
  return {source.severity == aipackaging::dxf::DiagnosticSeverity::Error, source.line, source.layer, source.entityType,
          source.message};
}

/// Формирует нейтральный результат анализа из предметного описания DXF.
PolygonImportInspection toApplicationInspection(const aipackaging::dxf::Inspection & source)
{
  PolygonImportInspection result;
  result.success = source.parsed;
  result.detectedUnit = fromDxfUnit(source.detectedUnit);
  result.unitSelectionRequired = source.unitSelectionRequired;
  for (const auto & layer : source.layers)
    result.layers.push_back({layer.name, layer.supportedEntities, layer.unsupportedEntities, true});
  for (const auto & path : source.paths)
    result.paths.push_back(
      {path.id, path.layer, path.closed, path.minX, path.minY, path.maxX, path.maxY, path.segmentCount, path.suggestedOuterPath});
  for (const auto & proposal : source.joinProposals)
    result.joinProposals.push_back(
      {proposal.id, proposal.firstPath, proposal.firstAtStart, proposal.secondPath, proposal.secondAtStart, proposal.distanceMm});
  for (const auto & diagnostic : source.diagnostics)
    result.diagnostics.push_back(toApplicationDiagnostic(diagnostic));
  if (!source.parsed)
  {
    const auto failure = std::find_if(source.diagnostics.rbegin(), source.diagnostics.rend(), [](const auto & diagnostic)
                                      { return diagnostic.severity == aipackaging::dxf::DiagnosticSeverity::Error; });
    result.error = failure == source.diagnostics.rend() ? "DXF не удалось разобрать" : failure->message;
  }
  return result;
}

/// Переносит подтверждённые настройки приложения во внутренний контракт импортёра.
aipackaging::dxf::ImportOptions toDxfOptions(const PolygonImportConfiguration & source)
{
  aipackaging::dxf::ImportOptions result;
  if (source.unit != PolygonImportUnit::Unknown)
    result.unitOverride = toDxfUnit(source.unit);
  result.selectedLayers = source.selectedLayers;
  result.joinToleranceMm = source.joinToleranceMm;
  result.acceptedJoinProposals = source.acceptedJoinProposals;
  result.ignoreUnsupportedOnSelectedLayers = source.ignoreUnsupported;
  result.groupDuplicateParts = source.groupDuplicateParts;
  for (const PolygonImportPart & part : source.parts)
    result.parts.push_back({part.outerPath, part.holes, part.partId, part.quantity, part.allowedRotations});
  result.problemId = source.problemId;
  result.sheetWidth = source.sheetWidth;
  result.sheetHeight = source.sheetHeight;
  result.sheetMargin = source.sheetMargin;
  result.partSpacing = source.partSpacing;
  result.kerf = source.kerf;
  result.curveTolerance = source.curveTolerance;
  return result;
}

/// Удерживает сеанс до подтверждённой передачи прикладному контроллеру.
class PendingInspection
{
public:
  /// Сохраняет шлюз и результат с зарегистрированным сеансом.
  PendingInspection(std::shared_ptr<IPolygonImportGateway> gateway, PolygonImportInspection result)
    : gateway_(std::move(gateway))
    , result_(std::move(result))
  {
  }
  /// Освобождает сеанс, если обработчик не принял владение.
  ~PendingInspection()
  {
    if (owned_ && result_.session && gateway_)
      gateway_->release(result_.session);
  }
  /// Возвращает копию результата для функции завершения.
  PolygonImportInspection result() const { return result_; }
  /// Передаёт владение сеансом прикладному контроллеру.
  void commit() noexcept { owned_ = false; }

private:
  std::shared_ptr<IPolygonImportGateway> gateway_;
  PolygonImportInspection result_;
  bool owned_ = true;
};

/// Удерживает точный снимок документа до подтверждённой передачи приложению.
class PendingImportResult
{
public:
  /// Сохраняет шлюз и построенный результат.
  PendingImportResult(std::shared_ptr<IPolygonImportGateway> gateway, PolygonImportBuildResult result)
    : gateway_(std::move(gateway))
    , result_(std::move(result))
  {
  }
  /// Освобождает не переданный точный снимок.
  ~PendingImportResult()
  {
    if (owned_ && gateway_)
      gateway_->release(result_);
  }
  /// Возвращает копию результата для функции завершения.
  PolygonImportBuildResult result() const { return result_; }
  /// Передаёт владение результатом прикладному контроллеру.
  void commit() noexcept { owned_ = false; }

private:
  std::shared_ptr<IPolygonImportGateway> gateway_;
  PolygonImportBuildResult result_;
  bool owned_ = true;
};
} // namespace

/// Сохраняет шлюзы, разделяющие импорт, редактируемую модель и точные снимки.
LocalPolygonImportGateway::LocalPolygonImportGateway(std::shared_ptr<IPolygonEditableDocumentGateway> editable,
                                                     std::shared_ptr<IPolygonDocumentGateway> documents)
  : editable_(std::move(editable))
  , documents_(std::move(documents))
{
}

/// Читает файл, анализирует содержимое и регистрирует его только после успеха синтаксического разбора.
PolygonImportInspection LocalPolygonImportGateway::inspect(const PolygonImportInspectionRequest & request)
{
  std::string contents;
  std::string error;
  if (!readDxfFile(request.filePath, contents, error))
    return {.error = std::move(error)};
  std::optional<aipackaging::dxf::LengthUnit> unit;
  if (request.unit != PolygonImportUnit::Unknown)
    unit = toDxfUnit(request.unit);
  PolygonImportInspection result = toApplicationInspection(
    aipackaging::dxf::inspectAsciiDxf(contents, unit, request.joinToleranceMm, request.acceptedJoinProposals));
  result.sourceIdentifier = request.filePath;
  if (!result.success)
    return result;
  const auto fingerprint = editable_->sourceFingerprint(request.filePath);
  std::lock_guard lock(mutex_);
  const PolygonImportSessionHandle handle{nextSession_++};
  sessions_.emplace(handle.value, Session{request.filePath, std::move(contents), fingerprint});
  result.session = handle;
  return result;
}

/// Копирует неизменяемый сеанс, строит документ и передаёт его существующей точной проверке.
PolygonImportBuildResult LocalPolygonImportGateway::build(PolygonImportSessionHandle session,
                                                          const PolygonImportConfiguration & configuration)
{
  Session stored;
  {
    std::lock_guard lock(mutex_);
    const auto found = sessions_.find(session.value);
    if (found == sessions_.end())
      return {.error = "Сеанс импорта больше недоступен"};
    stored = found->second;
  }
  aipackaging::dxf::ImportResult imported = aipackaging::dxf::importAsciiDxf(stored.contents, toDxfOptions(configuration));
  PolygonImportBuildResult result;
  for (const auto & diagnostic : imported.diagnostics)
    result.diagnostics.push_back(toApplicationDiagnostic(diagnostic));
  if (!imported.built)
  {
    result.error = "Параметры импорта содержат блокирующие ошибки";
    return result;
  }
  result.document = editable_->compileImported(std::move(imported.document), stored.filePath, stored.fingerprint);
  result.success = result.document.success;
  if (!result.success)
    result.error = result.document.error;
  return result;
}

/// Удаляет только совпадающий процессный сеанс.
void LocalPolygonImportGateway::release(PolygonImportSessionHandle session) noexcept
{
  try
  {
    std::lock_guard lock(mutex_);
    sessions_.erase(session.value);
  }
  catch (...)
  {
    return;
  }
}

/// Освобождает зарегистрированный снимок через штатный шлюз документов.
void LocalPolygonImportGateway::release(const PolygonImportBuildResult & result) noexcept
{
  if (result.document.compiled && result.document.compiled->document && documents_)
    documents_->release(result.document.compiled->document);
}

/// Сохраняет шлюз и диспетчер; рабочий поток создаётся только по явной команде.
StdThreadPolygonImportJobRunner::StdThreadPolygonImportJobRunner(std::shared_ptr<IPolygonImportGateway> gateway,
                                                                 std::shared_ptr<IApplicationDispatcher> dispatcher)
  : gateway_(std::move(gateway))
  , dispatcher_(std::move(dispatcher))
{
}

/// Останавливает и присоединяет поток до уничтожения зависимостей композиции.
StdThreadPolygonImportJobRunner::~StdThreadPolygonImportJobRunner()
{
  if (worker_.joinable())
  {
    worker_.request_stop();
    worker_.join();
  }
}

/// Выполняет чтение и доставляет сеанс либо диагностируемый отказ.
std::optional<PolygonImportJobHandle> StdThreadPolygonImportJobRunner::inspect(const PolygonImportInspectionRequest & request,
                                                                               PolygonImportJobCallbacks callbacks,
                                                                               std::string & error)
{
  const auto gateway = gateway_;
  const auto dispatcher = dispatcher_;
  return startWorker(
    [this, gateway, dispatcher, request](const std::stop_token & stopToken, PolygonImportJobHandle job,
                                         PolygonImportJobCallbacks callbacks) mutable
    {
      PolygonImportInspection result;
      try
      {
        result = gateway->inspect(request);
      }
      catch (const std::exception & exception)
      {
        result.error = exception.what();
      }
      catch (...)
      {
        result.error = "Анализ DXF завершился неизвестной ошибкой";
      }
      clearActiveJob(mutex_, activeJob_, job);
      if (stopToken.stop_requested())
      {
        if (result.session)
          gateway->release(result.session);
        if (callbacks.cancelled)
          postSafely(dispatcher,
                     [callback = std::move(callbacks.cancelled), job]() mutable
                     {
                       return [callback = std::move(callback), job]() noexcept
                       {
                         try
                         {
                           callback(job);
                         }
                         catch (...)
                         {
                           return;
                         }
                       };
                     });
        return;
      }
      if (!result.success)
      {
        if (callbacks.failed)
          postSafely(dispatcher,
                     [callback = std::move(callbacks.failed), job, error = result.error]() mutable
                     {
                       return [callback = std::move(callback), job, error]() noexcept
                       {
                         try
                         {
                           callback(job, error);
                         }
                         catch (...)
                         {
                           return;
                         }
                       };
                     });
        return;
      }
      try
      {
        auto pending = std::make_shared<PendingInspection>(gateway, std::move(result));
        if (callbacks.inspected)
          postSafely(dispatcher,
                     [callback = std::move(callbacks.inspected), pending, job]() mutable
                     {
                       return [callback = std::move(callback), pending, job]() noexcept
                       {
                         try
                         {
                           if (callback(job, pending->result()))
                             pending->commit();
                         }
                         catch (...)
                         {
                           return;
                         }
                       };
                     });
      }
      catch (...)
      {
        if (result.session)
          gateway->release(result.session);
      }
    },
    std::move(callbacks), error);
}

/// Выполняет построение и удерживает точный снимок до подтверждения прикладного обработчика.
std::optional<PolygonImportJobHandle> StdThreadPolygonImportJobRunner::build(PolygonImportSessionHandle session,
                                                                             const PolygonImportConfiguration & configuration,
                                                                             PolygonImportJobCallbacks callbacks,
                                                                             std::string & error)
{
  const auto gateway = gateway_;
  const auto dispatcher = dispatcher_;
  return startWorker(
    [this, gateway, dispatcher, session, configuration](const std::stop_token & stopToken, PolygonImportJobHandle job,
                                                        PolygonImportJobCallbacks callbacks) mutable
    {
      PolygonImportBuildResult result;
      try
      {
        result = gateway->build(session, configuration);
      }
      catch (const std::exception & exception)
      {
        result.error = exception.what();
      }
      catch (...)
      {
        result.error = "Построение документа завершилось неизвестной ошибкой";
      }
      clearActiveJob(mutex_, activeJob_, job);
      if (stopToken.stop_requested())
      {
        gateway->release(result);
        if (callbacks.cancelled)
          postSafely(dispatcher,
                     [callback = std::move(callbacks.cancelled), job]() mutable
                     {
                       return [callback = std::move(callback), job]() noexcept
                       {
                         try
                         {
                           callback(job);
                         }
                         catch (...)
                         {
                           return;
                         }
                       };
                     });
        return;
      }
      if (!result.success)
      {
        gateway->release(result);
        if (callbacks.failed)
          postSafely(dispatcher,
                     [callback = std::move(callbacks.failed), job, failure = result.error]() mutable
                     {
                       return [callback = std::move(callback), job, failure]() noexcept
                       {
                         try
                         {
                           callback(job, failure);
                         }
                         catch (...)
                         {
                           return;
                         }
                       };
                     });
        return;
      }
      try
      {
        auto pending = std::make_shared<PendingImportResult>(gateway, std::move(result));
        if (callbacks.completed)
          postSafely(dispatcher,
                     [callback = std::move(callbacks.completed), pending, job]() mutable
                     {
                       return [callback = std::move(callback), pending, job]() noexcept
                       {
                         try
                         {
                           if (callback(job, pending->result()))
                             pending->commit();
                         }
                         catch (...)
                         {
                           return;
                         }
                       };
                     });
      }
      catch (...)
      {
        gateway->release(result);
      }
    },
    std::move(callbacks), error);
}

/// Проверяет единственность работы, присоединяет прежний поток и запускает новый.
std::optional<PolygonImportJobHandle> StdThreadPolygonImportJobRunner::startWorker(
  std::function<void(const std::stop_token &, PolygonImportJobHandle, PolygonImportJobCallbacks)> work,
  PolygonImportJobCallbacks callbacks, std::string & error)
{
  std::lock_guard lock(mutex_);
  if (activeJob_)
  {
    error = "Другая операция импорта уже выполняется";
    return std::nullopt;
  }
  if (worker_.joinable())
    worker_.join();
  const PolygonImportJobHandle job{nextJob_++};
  activeJob_ = job;
  try
  {
    worker_ = std::jthread(
      [this, work = std::move(work), callbacks = std::move(callbacks), job](const std::stop_token & token) mutable noexcept
      {
        try
        {
          work(token, job, std::move(callbacks));
        }
        catch (...)
        {
          clearActiveJob(mutex_, activeJob_, job);
        }
      });
  }
  catch (const std::exception & exception)
  {
    activeJob_.reset();
    error = exception.what();
    return std::nullopt;
  }
  catch (...)
  {
    activeJob_.reset();
    error = "Не удалось создать рабочий поток импорта";
    return std::nullopt;
  }
  return job;
}

/// Запрашивает остановку только совпадающей актуальной работы.
void StdThreadPolygonImportJobRunner::cancel(PolygonImportJobHandle job) noexcept
{
  std::lock_guard lock(mutex_);
  if (activeJob_ == job && worker_.joinable())
    worker_.request_stop();
}

/// Освобождает временный сеанс через предметный шлюз.
void StdThreadPolygonImportJobRunner::release(PolygonImportSessionHandle session) noexcept
{
  gateway_->release(session);
}

/// Освобождает точный снимок результата через предметный шлюз.
void StdThreadPolygonImportJobRunner::release(const PolygonImportBuildResult & result) noexcept
{
  gateway_->release(result);
}
