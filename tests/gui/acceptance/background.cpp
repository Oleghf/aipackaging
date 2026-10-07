#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QWizard>
#include <thread>

#include <gtest/gtest.h>
#include <polygoncanvaswidget.h>
#include <polygondxfimportwizard.h>

#include "support.h"

namespace acceptance
{
namespace
{
/// Удерживает внешнюю операцию проверяемым барьером, ограничивая неисправность теста десятью секундами.
struct OperationGate
{
  std::mutex mutex;
  std::condition_variable changed;
  bool released = false;
  std::atomic<bool> entered = false;
  std::atomic<int> completed = 0;
  std::atomic<int> freed = 0;
  std::atomic<int> resultFreed = 0;
  std::atomic<bool> cancelled = false;
  bool fail = false;
  bool holdBuild = false;

  /// Объявляет достижение барьера и ожидает явного разрешения без случайных задержек.
  void hold()
  {
    std::unique_lock lock(mutex);
    entered = true;
    if (!changed.wait_for(lock, std::chrono::seconds(10), [&]() { return released; }))
      throw std::runtime_error("Истёк срок тестового барьера");
    if (fail)
      throw std::runtime_error("Управляемый отказ приёмки");
  }
  /// Освобождает текущую и последующие операции барьера.
  void release()
  {
    std::lock_guard lock(mutex);
    released = true;
    changed.notify_all();
  }
};

/// Удерживает настоящий базовый решатель перед вычислением, сохраняя его отмену и результат.
class HeldBackend : public IPolygonNestingBackend
{
  std::shared_ptr<IPolygonNestingBackend> inner_;
  std::shared_ptr<OperationGate> gate_;

public:
  /// Сохраняет настоящий адаптер и общий барьер теста.
  HeldBackend(std::shared_ptr<IPolygonNestingBackend> inner, std::shared_ptr<OperationGate> gate)
    : inner_(std::move(inner))
    , gate_(std::move(gate))
  {
  }
  /// Ждёт барьер, затем передаёт неизменённый запрос настоящему решателю.
  NestingRunResult run(PolygonDocumentHandle document, const NestingRunRequest & request, const Control & control) override
  {
    gate_->hold();
    gate_->cancelled = control.cancellationRequested();
    try
    {
      auto result = inner_->run(document, request, control);
      ++gate_->completed;
      return result;
    }
    catch (...)
    {
      ++gate_->completed;
      throw;
    }
  }
};

/// Удерживает чтение DXF, не заменяя анализ, хранение сеанса и освобождение реального шлюза.
class HeldImport : public IPolygonImportGateway
{
  std::shared_ptr<IPolygonImportGateway> inner_;
  std::shared_ptr<OperationGate> gate_;

public:
  /// Сохраняет предметный шлюз и барьер анализа.
  HeldImport(std::shared_ptr<IPolygonImportGateway> inner, std::shared_ptr<OperationGate> gate)
    : inner_(std::move(inner))
    , gate_(std::move(gate))
  {
  }
  /// Удерживает анализ до закрытия окна, затем возвращает настоящий сеанс.
  PolygonImportInspection inspect(const PolygonImportInspectionRequest & request) override
  {
    if (!gate_->holdBuild)
      gate_->hold();
    auto result = inner_->inspect(request);
    ++gate_->completed;
    return result;
  }
  /// Передаёт построение без изменения геометрии и настроек.
  PolygonImportBuildResult build(PolygonImportSessionHandle session, const PolygonImportConfiguration & configuration) override
  {
    if (gate_->holdBuild)
      gate_->hold();
    auto result = inner_->build(session, configuration);
    ++gate_->completed;
    return result;
  }
  /// Считает освобождение сеанса, сохраняя настоящий механизм хранения.
  void release(PolygonImportSessionHandle session) noexcept override
  {
    ++gate_->freed;
    inner_->release(session);
  }
  /// Освобождает непринятый результат штатным адаптером.
  void release(const PolygonImportBuildResult & result) noexcept override
  {
    ++gate_->resultFreed;
    inner_->release(result);
  }
};

/// Удерживает только запись черновика, оставляя чтение и точную компиляцию настоящими.
class HeldDraft : public IPolygonEditableDocumentGateway
{
  std::shared_ptr<IPolygonEditableDocumentGateway> inner_;
  std::shared_ptr<OperationGate> gate_;

public:
  /// Сохраняет файловый адаптер и управляемый барьер записи.
  HeldDraft(std::shared_ptr<IPolygonEditableDocumentGateway> inner, std::shared_ptr<OperationGate> gate)
    : inner_(std::move(inner))
    , gate_(std::move(gate))
  {
  }
  /// Передаёт открытие задачи без подмены результата.
  PolygonEditableDocumentLoadResult load(const std::string & path) override { return inner_->load(path); }
  /// Передаёт восстановление штатному файловому адаптеру.
  PolygonEditableDocumentLoadResult loadRecovery(const std::string & path) override { return inner_->loadRecovery(path); }
  /// Сохраняет обе проверки импортированного или изменённого документа.
  PolygonEditableDocumentLoadResult compileImported(aipackaging::editor::EditablePolygonDocument document,
                                                    const std::string & source,
                                                    std::optional<PolygonSourceFingerprint> fingerprint) override
  {
    return inner_->compileImported(std::move(document), source, fingerprint);
  }
  /// Сохраняет пользовательскую задачу без задержки.
  PolygonDocumentOperationResult saveProblem(const std::string & path,
                                             const aipackaging::editor::EditablePolygonDocument & document) override
  {
    return inner_->saveProblem(path, document);
  }
  /// Ждёт барьер перед настоящей атомарной записью.
  PolygonDocumentOperationResult saveDraft(const std::string & path,
                                           const aipackaging::editor::EditablePolygonDocument & document,
                                           PolygonDocumentSource source, const std::string & identifier, std::uint64_t generation,
                                           std::optional<PolygonSourceFingerprint> fingerprint) override
  {
    gate_->hold();
    auto result = inner_->saveDraft(path, document, source, identifier, generation, fingerprint);
    ++gate_->completed;
    return result;
  }
  /// Получает настоящий отпечаток файла.
  std::optional<PolygonSourceFingerprint> sourceFingerprint(const std::string & path) override
  {
    return inner_->sourceFingerprint(path);
  }
  /// Читает настоящую карточку восстановления.
  PolygonRecoveryCandidate inspectRecovery(const std::string & path) override { return inner_->inspectRecovery(path); }
  /// Удаляет черновик штатной последовательной операцией.
  PolygonDocumentOperationResult removeRecovery(const std::string & path) override { return inner_->removeRecovery(path); }
};

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
/// Удерживает загрузку синтетической модели, сохраняя настоящий ONNX-сеанс.
class HeldModel : public IPolygonModelGateway
{
  std::shared_ptr<IPolygonModelGateway> inner_;
  std::shared_ptr<OperationGate> gate_;

public:
  /// Сохраняет штатный шлюз и барьер загрузки.
  HeldModel(std::shared_ptr<IPolygonModelGateway> inner, std::shared_ptr<OperationGate> gate)
    : inner_(std::move(inner))
    , gate_(std::move(gate))
  {
  }
  /// Ждёт барьер и загружает настоящий комплект модели.
  PolygonModelLoadResult load(const std::string & path) override
  {
    gate_->hold();
    auto result = inner_->load(path);
    ++gate_->completed;
    return result;
  }
  /// Считает освобождение непринятой модели.
  void release(PolygonModelHandle model) noexcept override
  {
    ++gate_->freed;
    inner_->release(model);
  }
};
#endif

/// Освобождает операцию только после начала разрушения владельца, проверяя присоединение потока.
template<class T>
std::jthread releaseOnDestruction(const std::weak_ptr<T> & owner, const std::shared_ptr<OperationGate> & gate)
{
  return std::jthread(
    [owner, gate]()
    {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!owner.expired() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
      gate->release();
    });
}
} // namespace

/// Изолирует управляемые фоновые сценарии от пользовательских настроек.
class GuiBackgroundAcceptance : public testing::Test
{
protected:
  QTemporaryDir directory_;
  /// Сбрасывает профиль только текущего тестового процесса.
  void SetUp() override
  {
    ASSERT_TRUE(directory_.isValid());
    QSettings().clear();
  }
  /// Возвращает неизменяемый поставляемый вход; все записи теста направляются отдельно.
  QString problem() const { return QStringLiteral(AIPACKAGING_SOURCE_ROOT "/examples/polygon/problem-small.json"); }
};

/// Проверяет историю после успеха, отмены и отказа, удерживая настоящий рабочий поток.
TEST_F(GuiBackgroundAcceptance, HistoryRestoredAfterEverySearchOutcome)
{
  for (const int outcome : {0, 1, 2})
  {
    SCOPED_TRACE(outcome);
    QSettings().clear();
    auto gate = std::make_shared<OperationGate>();
    gate->fail = outcome == 2;
    DesktopHooks hooks;
    hooks.backend = [gate](auto inner)
    {
      return std::make_shared<HeldBackend>(std::move(inner), gate);
    };
    Desktop app(directory_.path(), hooks);
    app.open(problem());
    app.changeWidth(250);
    ASSERT_TRUE(app.workspace->snapshot().canUndo);
    key(&app.window, Qt::Key_Return, Qt::ControlModifier);
    ASSERT_TRUE(waitUntil([&]() { return gate->entered.load(); }));
    EXPECT_FALSE(app.workspace->snapshot().canUndo);
    EXPECT_FALSE(app.workspace->snapshot().canEdit);
    if (outcome == 1)
      key(widget<PolygonCanvasWidget>(app.window, "polygonCanvas"), Qt::Key_Escape);
    gate->release();
    ASSERT_TRUE(waitUntil([&]() { return app.workspace->snapshot().state != PolygonWorkspaceState::Running; }));
    EXPECT_TRUE(app.workspace->snapshot().canUndo);
    EXPECT_TRUE(app.workspace->snapshot().canEdit);
    auto * canvas = widget<PolygonCanvasWidget>(app.window, "polygonCanvas");
    key(canvas, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_FALSE(app.workspace->snapshot().documentDirty);
    EXPECT_TRUE(app.workspace->snapshot().canRedo);
    key(canvas, Qt::Key_Y, Qt::ControlModifier);
    EXPECT_TRUE(app.workspace->snapshot().documentDirty);
    if (outcome == 1)
      EXPECT_TRUE(gate->cancelled);
  }
}

/// Закрывает настоящее окно с удерживаемым поиском и проверяет отмену и завершение потока при разрушении.
TEST_F(GuiBackgroundAcceptance, CloseDuringHeldSearch)
{
  auto gate = std::make_shared<OperationGate>();
  DesktopHooks hooks;
  hooks.backend = [gate](auto inner)
  {
    return std::make_shared<HeldBackend>(std::move(inner), gate);
  };
  auto app = std::make_unique<Desktop>(directory_.path(), hooks);
  app->open(problem());
  key(&app->window, Qt::Key_Return, Qt::ControlModifier);
  ASSERT_TRUE(waitUntil([&]() { return gate->entered.load(); }));
  EXPECT_TRUE(app->window.close());
  auto releaser = releaseOnDestruction(std::weak_ptr(app->workspace), gate);
  QPointer<QWidget> window = &app->window;
  app.reset();
  EXPECT_TRUE(window.isNull());
  EXPECT_EQ(gate->completed, 1);
  EXPECT_TRUE(gate->cancelled);
  QCoreApplication::processEvents();
}

/// Закрывает мастер вместе с окном во время анализа и освобождает непринятый настоящий сеанс ровно один раз.
TEST_F(GuiBackgroundAcceptance, CloseDuringHeldImport)
{
  for (const bool build : {false, true})
  {
    SCOPED_TRACE(build);
    QSettings().clear();
    auto gate = std::make_shared<OperationGate>();
    gate->holdBuild = build;
    DesktopHooks hooks;
    hooks.imports = [gate](auto inner)
    {
      return std::make_shared<HeldImport>(std::move(inner), gate);
    };
    auto app = std::make_unique<Desktop>(directory_.path(), hooks);
    chooseFile(QStringLiteral(AIPACKAGING_SOURCE_ROOT "/examples/dxf/square-mm.dxf"));
    action(app->window, "importDxfAction");
    if (build)
    {
      ASSERT_TRUE(waitUntil([&]() { return app->importer->snapshot().state == PolygonImportState::Ready; }));
      auto * wizard = widget<PolygonDxfImportWizard>(app->window, "polygonDxfImportWizard");
      for (int page = 0; page < 4; ++page)
        key(wizard->button(QWizard::NextButton), Qt::Key_Space);
      key(wizard->button(QWizard::FinishButton), Qt::Key_Space);
    }
    ASSERT_TRUE(waitUntil([&]() { return gate->entered.load(); }));
    EXPECT_TRUE(app->window.close());
    auto releaser = releaseOnDestruction(std::weak_ptr(app->importer), gate);
    app.reset();
    EXPECT_EQ(gate->completed, build ? 2 : 1);
    EXPECT_EQ(gate->freed, 1);
    EXPECT_EQ(gate->resultFreed, build ? 1 : 0);
    QCoreApplication::processEvents();
  }
}

/// Проверяет отмену закрытия и упорядоченное удаление удерживаемой записи при подтверждённом отказе от изменений.
TEST_F(GuiBackgroundAcceptance, CloseDuringHeldDraft)
{
  auto gate = std::make_shared<OperationGate>();
  DesktopHooks hooks;
  hooks.editable = [gate](auto inner)
  {
    return std::make_shared<HeldDraft>(std::move(inner), gate);
  };
  auto app = std::make_unique<Desktop>(directory_.path(), hooks);
  app->open(problem());
  app->changeWidth(250);
  ASSERT_TRUE(waitUntil([&]() { return gate->entered.load(); }));
  whenModal([](QWidget * dialog) { click(qobject_cast<QMessageBox *>(dialog)->button(QMessageBox::Cancel)); });
  EXPECT_FALSE(app->window.close());
  EXPECT_TRUE(app->workspace->snapshot().documentDirty);
  // Нормальное закрытие вправе ждать запись; разрешение отдаёт независимый источник,
  // а не будущий деструктор, который ещё не может начаться внутри closeEvent.
  whenModal(
    [gate](QWidget * dialog)
    {
      gate->release();
      click(qobject_cast<QMessageBox *>(dialog)->button(QMessageBox::Discard));
    });
  EXPECT_TRUE(app->window.close());
  auto releaser = releaseOnDestruction(std::weak_ptr(app->document), gate);
  app.reset();
  EXPECT_GE(gate->completed, 1);
  EXPECT_FALSE(QFile::exists(directory_.filePath("active.aipdraft.json")));
  QCoreApplication::processEvents();
}

/// Проверяет отмену из фактического фокуса после блокировки редактора, не переназначая его полотну.
TEST_F(GuiBackgroundAcceptance, EscapeAfterStartingFromEachFocus)
{
  for (int focus = 0; focus < 4; ++focus)
  {
    SCOPED_TRACE(focus);
    QSettings().clear();
    auto gate = std::make_shared<OperationGate>();
    DesktopHooks hooks;
    hooks.backend = [gate](auto inner)
    {
      return std::make_shared<HeldBackend>(std::move(inner), gate);
    };
    Desktop app(directory_.path(), hooks);
    app.open(problem());
    auto * tabs = widget<QTabWidget>(app.window, "workspaceRightTabs");
    click(tabs->tabBar(), tabs->tabBar()->tabRect(focus == 3 ? 1 : 0).center());
    QWidget * target = nullptr;
    if (focus == 0)
      target = widget<PolygonCanvasWidget>(app.window, "polygonCanvas");
    else if (focus == 1)
      target = widget<QTreeWidget>(app.window, "editorEntityTree");
    else if (focus == 2)
      target = widget<QLineEdit>(app.window, "editorProblemId");
    else
      target = widget<QComboBox>(app.window, "polygonSolverBox");
    key(target, Qt::Key_Return, Qt::ControlModifier);
    ASSERT_TRUE(waitUntil([&]() { return gate->entered.load(); }));
    QWidget * actualFocus = QApplication::focusWidget();
    // Qt направляет клавишу активному окну, если отключённое поле утратило фокус.
    if (!actualFocus)
      actualFocus = &app.window;
    SCOPED_TRACE(actualFocus->objectName().toStdString());
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, {});
    QApplication::sendEvent(actualFocus, &escape);
    gate->release();
    ASSERT_TRUE(waitUntil([&]() { return app.workspace->snapshot().state != PolygonWorkspaceState::Running; }));
    EXPECT_TRUE(gate->cancelled);
    EXPECT_EQ(app.workspace->snapshot().state, PolygonWorkspaceState::Cancelled);
  }
}

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
/// Закрывает окно во время чтения модели и проверяет освобождение не переданного ONNX-сеанса.
TEST_F(GuiBackgroundAcceptance, CloseDuringHeldModel)
{
  auto gate = std::make_shared<OperationGate>();
  DesktopHooks hooks;
  hooks.models = [gate](auto inner)
  {
    return std::make_shared<HeldModel>(std::move(inner), gate);
  };
  auto app = std::make_unique<Desktop>(directory_.path(), hooks);
  chooseFile(QStringLiteral(AIPACKAGING_SOURCE_ROOT "/tests/fixtures/models/polygon-policy-smoke"));
  action(app->window, "openModelAction");
  ASSERT_TRUE(waitUntil([&]() { return gate->entered.load(); }));
  EXPECT_TRUE(app->window.close());
  auto releaser = releaseOnDestruction(std::weak_ptr(app->workspace), gate);
  app.reset();
  EXPECT_EQ(gate->completed, 1);
  EXPECT_EQ(gate->freed, 1);
  QCoreApplication::processEvents();
}
#endif
} // namespace acceptance
