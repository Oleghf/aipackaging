#ifndef AIPACKAGING_APPLICATION_POLYGONWORKSPACECONTROLLER_H
#define AIPACKAGING_APPLICATION_POLYGONWORKSPACECONTROLLER_H

#include <memory>
#include <optional>
#include <string>

#include <activepolygondocument.h>
#include <polygonworkspaceports.h>

/// Управляет полигональным пользовательским сценарием как однопоточный автомат состояния.
class PolygonWorkspaceController : public std::enable_shared_from_this<PolygonWorkspaceController>
{
public:
  /// Создаёт контроллер поверх прикладных портов документов, выполнения и вывода.
  PolygonWorkspaceController(std::shared_ptr<IPolygonWorkspaceOutput> output, std::shared_ptr<IPolygonDocumentGateway> documents,
                             std::shared_ptr<INestingJobRunner> jobs, std::shared_ptr<IPolygonModelGateway> models = {});
  /// Освобождает документы и запрашивает отмену активной работы.
  ~PolygonWorkspaceController();
  /// Формирует безопасные слабые обработчики действий для интерфейса.
  PolygonWorkspaceActions actions();
  /// Загружает документ, сохраняя прежнюю сцену при ошибке.
  void openProblem(const std::string & filePath);
  /// Сохраняет последнее разрешённое и проверенное решение.
  void saveSolution(const std::string & filePath);
  /// Загружает внешний комплект модели, сохраняя прежний при ошибке.
  /// Загружает и проверяет комплект модели; возвращает успех без изменения прежней модели при ошибке.
  bool openModel(const std::string & directory);
  /// Запускает выбранный способ раскроя, если задача готова.
  void start(const NestingRunRequest & request);
  /// Запрашивает отмену текущей работы без блокировки вызывающего потока.
  void cancel();
  /// Возвращает последний опубликованный снимок модели представления.
  PolygonWorkspaceSnapshot snapshot() const;

private:
  /// Публикует состояние и вычисляет доступность действий.
  void publish();
  /// Принимает сообщение о ходе актуальной работы.
  void acceptProgress(NestingJobHandle job, const NestingProgress & progress);
  /// Принимает подготовленный результат актуальной работы.
  void acceptResult(NestingJobHandle job, NestingRunResult result);
  /// Завершает актуальную работу диагностируемой ошибкой.
  void acceptFailure(NestingJobHandle job, const std::string & error);
  /// Освобождает текущий сохраняемый результат.
  void releaseSolution() noexcept;
  /// Освобождает текущий комплект модели.
  void releaseModel() noexcept;

  std::shared_ptr<IPolygonWorkspaceOutput> output_;
  std::shared_ptr<IPolygonDocumentGateway> documents_;
  std::shared_ptr<INestingJobRunner> jobs_;
  std::shared_ptr<IPolygonModelGateway> models_;
  ActivePolygonDocument document_;
  std::optional<PolygonSolutionHandle> solution_;
  std::optional<PolygonModelHandle> model_;
  std::optional<NestingJobHandle> activeJob_;
  PolygonWorkspaceSnapshot snapshot_;
};

#endif
