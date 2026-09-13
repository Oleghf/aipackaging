#ifndef AIPACKAGING_APP_POLYGONWORKSPACECONTROLLER_H
#define AIPACKAGING_APP_POLYGONWORKSPACECONTROLLER_H

#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <polygonenvironment.h>
#include <polygonworkspaceview.h>

/// Абстракция внутренней реализации полигонального поиска для базовых алгоритмов и модели.
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

/// Управляет загрузкой, асинхронным поиском, проверкой и сохранением `polygon_solution`.
class PolygonWorkspaceController : public std::enable_shared_from_this<PolygonWorkspaceController>
{
public:
  /// Создаёт контроллер для представления и необязательной тестовой реализации.
  explicit PolygonWorkspaceController(std::shared_ptr<IPolygonWorkspaceView> view,
                                      std::shared_ptr<IPolygonSolverBackend> backend = {});
  /// Останавливает активную работу и дожидается завершения рабочего потока.
  ~PolygonWorkspaceController();

  /// Привязывает безопасные слабые функции обратного вызова к представлению.
  void bindActions();
  /// Загружает строгий `polygon_problem`, сохраняя прежнюю сцену при ошибке.
  void openProblem(const std::string & filePath);
  /// Сохраняет последнее разрешённое и независимо проверенное решение `polygon_solution`.
  void saveSolution(const std::string & filePath);
  /// Запускает выбранный базовый алгоритм асинхронно, если задача готова.
  void start(const aipackaging::solver::SolverConfig & config);
  /// Запрашивает отмену текущего поиска без блокировки GUI.
  void cancel();
  /// Возвращает последний опубликованный снимок модели представления.
  PolygonWorkspaceSnapshot snapshot() const;

private:
  /// Публикует текущее состояние и вычисляет доступность действий.
  void publish();
  /// Принимает актуальное сообщение о ходе выполнения в потоке UI.
  void acceptProgress(std::uint64_t runId, const aipackaging::solver::PolygonSolverProgress & progress);
  /// Принимает результат актуального рабочего потока и повторно проверяет его.
  void acceptResult(std::uint64_t runId, aipackaging::solver::PolygonSolverExecutionResult result);
  /// Завершает актуальный запуск диагностируемой ошибкой внутренней реализации.
  void acceptFailure(std::uint64_t runId, const std::string & error);
  /// Формирует сцену модели представления и список неразмещённых экземпляров.
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
