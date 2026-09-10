#ifndef AIPACKAGING_APP_POLYGONWORKSPACECONTROLLER_H
#define AIPACKAGING_APP_POLYGONWORKSPACECONTROLLER_H

#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <polygonenvironment.h>
#include <polygonworkspaceview.h>

/// Абстракция backend-а полигонального поиска для baseline и будущей модели.
class IPolygonSolverBackend
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonSolverBackend() = default;
  /// Выполняет поиск с заданными настройками и внешним управлением.
  virtual aipackaging::solver::PolygonSolverExecutionResult run(const aipackaging::solver::PolygonProblem & problem,
                                                                const aipackaging::solver::SolverConfig & config,
                                                                const aipackaging::solver::PolygonExecutionControl & control) = 0;
};

/// Оркестрирует загрузку, асинхронный поиск, проверку и сохранение polygon_solution.
class PolygonWorkspaceController : public std::enable_shared_from_this<PolygonWorkspaceController>
{
public:
  /// Создаёт контроллер для представления и необязательного тестового backend-а.
  explicit PolygonWorkspaceController(std::shared_ptr<IPolygonWorkspaceView> view,
                                      std::shared_ptr<IPolygonSolverBackend> backend = {});
  /// Останавливает активную работу и дожидается завершения worker-потока.
  ~PolygonWorkspaceController();

  /// Привязывает безопасные weak-callback пользовательских действий к представлению.
  void bindActions();
  /// Загружает strict polygon_problem, не разрушая прежнюю сцену при ошибке.
  void openProblem(const std::string & filePath);
  /// Сохраняет последний разрешённый independently validated polygon_solution.
  void saveSolution(const std::string & filePath);
  /// Запускает выбранный baseline асинхронно, если задача готова.
  void start(const aipackaging::solver::SolverConfig & config);
  /// Запрашивает отмену текущего поиска без блокировки GUI.
  void cancel();
  /// Возвращает последний опубликованный presentation-снимок.
  PolygonWorkspaceSnapshot snapshot() const;

private:
  /// Публикует текущее состояние и вычисляет доступность действий.
  void publish();
  /// Принимает актуальный progress callback в UI-потоке.
  void acceptProgress(std::uint64_t runId, const aipackaging::solver::PolygonSolverProgress & progress);
  /// Принимает результат актуального worker-а и повторно проверяет его.
  void acceptResult(std::uint64_t runId, aipackaging::solver::PolygonSolverExecutionResult result);
  /// Завершает актуальный запуск диагностируемой внутренней ошибкой backend-а.
  void acceptFailure(std::uint64_t runId, const std::string & error);
  /// Формирует presentation-сцену и список неразмещённых экземпляров.
  void rebuildPresentation(const aipackaging::solver::PolygonSolution * solution);

  std::shared_ptr<IPolygonWorkspaceView> view_;
  std::shared_ptr<IPolygonSolverBackend> backend_;
  std::optional<aipackaging::solver::PolygonProblem> problem_;
  std::unique_ptr<aipackaging::solver::PolygonEnvironment> environment_;
  std::optional<aipackaging::solver::PolygonSolution> solution_;
  PolygonWorkspaceSnapshot snapshot_;
  std::jthread worker_;
  std::uint64_t runId_ = 0;
  bool saveable_ = false;
};

#endif
