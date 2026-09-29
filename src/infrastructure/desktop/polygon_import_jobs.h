#ifndef AIPACKAGING_INFRASTRUCTURE_POLYGON_IMPORT_JOBS_H
#define AIPACKAGING_INFRASTRUCTURE_POLYGON_IMPORT_JOBS_H

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <polygonimportcontracts.h>
#include <polygonworkspaceports.h>

/// Читает ASCII DXF, хранит временные сеансы и компилирует импортированный документ.
class LocalPolygonImportGateway final : public IPolygonImportGateway
{
public:
  /// Связывает импортёр с существующими шлюзами редактируемых и точных документов.
  LocalPolygonImportGateway(std::shared_ptr<IPolygonEditableDocumentGateway> editable,
                            std::shared_ptr<IPolygonDocumentGateway> documents);
  /// Читает обычный файл и регистрирует только успешно разобранный сеанс.
  PolygonImportInspection inspect(const PolygonImportInspectionRequest & request) override;
  /// Строит модель по сохранённому содержимому и выполняет точную проверку.
  PolygonImportBuildResult build(PolygonImportSessionHandle session, const PolygonImportConfiguration & configuration) override;
  /// Удаляет временный сеанс без исключений.
  void release(PolygonImportSessionHandle session) noexcept override;
  /// Освобождает точный снимок не принятого результата.
  void release(const PolygonImportBuildResult & result) noexcept override;

private:
  /// Хранит неизменяемое содержимое и отпечаток одного исходного файла.
  struct Session
  {
    std::string filePath;
    std::string contents;
    std::optional<PolygonSourceFingerprint> fingerprint;
  };

  std::shared_ptr<IPolygonEditableDocumentGateway> editable_;
  std::shared_ptr<IPolygonDocumentGateway> documents_;
  std::mutex mutex_;
  std::uint64_t nextSession_ = 1;
  std::map<std::uint64_t, Session> sessions_;
};

/// Выполняет анализ и построение импортированного документа в одном управляемом потоке.
class StdThreadPolygonImportJobRunner final : public IPolygonImportJobRunner
{
public:
  /// Сохраняет шлюз импорта и диспетчер прикладного потока.
  StdThreadPolygonImportJobRunner(std::shared_ptr<IPolygonImportGateway> gateway,
                                  std::shared_ptr<IApplicationDispatcher> dispatcher);
  /// Запрашивает отмену и присоединяет активный поток.
  ~StdThreadPolygonImportJobRunner() override;
  /// Запускает фоновое чтение и анализ одного файла.
  std::optional<PolygonImportJobHandle> inspect(const PolygonImportInspectionRequest & request,
                                                PolygonImportJobCallbacks callbacks, std::string & error) override;
  /// Запускает фоновое построение документа по сохранённому сеансу.
  std::optional<PolygonImportJobHandle> build(PolygonImportSessionHandle session,
                                              const PolygonImportConfiguration & configuration,
                                              PolygonImportJobCallbacks callbacks, std::string & error) override;
  /// Запрашивает отмену совпадающей работы.
  void cancel(PolygonImportJobHandle job) noexcept override;
  /// Передаёт освобождение сеанса предметному шлюзу.
  void release(PolygonImportSessionHandle session) noexcept override;
  /// Передаёт освобождение результата предметному шлюзу.
  void release(const PolygonImportBuildResult & result) noexcept override;

private:
  /// Запускает подготовленную невыбрасывающую функцию рабочего потока.
  std::optional<PolygonImportJobHandle>
  startWorker(std::function<void(const std::stop_token &, PolygonImportJobHandle, PolygonImportJobCallbacks)> work,
              PolygonImportJobCallbacks callbacks, std::string & error);

  std::shared_ptr<IPolygonImportGateway> gateway_;
  std::shared_ptr<IApplicationDispatcher> dispatcher_;
  std::mutex mutex_;
  std::jthread worker_;
  std::uint64_t nextJob_ = 1;
  std::optional<PolygonImportJobHandle> activeJob_;
};

#endif
