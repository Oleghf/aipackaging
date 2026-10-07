#include <algorithm>
#include <cmath>
#include <future>
#include <iostream>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QMouseEvent>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <vector>

#include <aipackaging/editor/polygon_draft_io.h>
#include <polygoncanvaswidget.h>
#include <polygoneditorcanvasinteraction.h>

#include "support.h"

namespace acceptance
{
namespace
{
using namespace aipackaging::editor;
/// Создаёт документ с 25 сегментами на деталь, кривыми и отверстием без случайных входов.
EditablePolygonDocument largeDocument(int segments)
{
  EditablePolygonDocument result;
  result.problemId = "gui-performance-" + std::to_string(segments);
  result.sheet.width = 2000;
  result.sheet.height = 1000;
  const int count = segments / 25;
  for (int partIndex = 0; partIndex < count; ++partIndex)
  {
    EditablePart part;
    part.id = result.allocateEntityId();
    part.partId = "part-" + std::to_string(partIndex);
    part.quantity = static_cast<std::uint32_t>(100 / count + (partIndex < 100 % count ? 1 : 0));
    part.allowedRotations = {0, 90};
    EditablePath outer;
    outer.id = result.allocateEntityId();
    outer.closed = true;
    for (int index = 0; index < 21; ++index)
    {
      const double angle = 2 * std::acos(-1.0) * static_cast<double>(index) / 21.0;
      outer.vertices.push_back({result.allocateEntityId(), 40 + 30 * std::cos(angle), 40 + 30 * std::sin(angle)});
      outer.segments.push_back({result.allocateEntityId(), EditableSegmentKind::Line});
    }
    outer.segments[0].kind = EditableSegmentKind::Arc;
    outer.segments[0].center = {result.allocateEntityId(), 40, 40};
    auto & bezier = outer.segments[1];
    bezier.kind = EditableSegmentKind::CubicBezier;
    const auto a = outer.vertices[1];
    const auto b = outer.vertices[2];
    bezier.control1 = {result.allocateEntityId(), (2 * a.x + b.x) / 3, (2 * a.y + b.y) / 3};
    bezier.control2 = {result.allocateEntityId(), (a.x + 2 * b.x) / 3, (a.y + 2 * b.y) / 3};
    part.outer = std::move(outer);
    EditablePath hole;
    hole.id = result.allocateEntityId();
    hole.closed = true;
    for (const auto & position : {QPointF(35, 35), QPointF(45, 35), QPointF(45, 45), QPointF(35, 45)})
    {
      hole.vertices.push_back({result.allocateEntityId(), position.x(), position.y()});
      hole.segments.push_back({result.allocateEntityId(), EditableSegmentKind::Line});
    }
    part.holes.push_back(std::move(hole));
    result.parts.push_back(std::move(part));
  }
  return result;
}
/// Измеряет завершённую операцию и обслуживает вызванные ей события интерфейса.
double elapsed(const std::function<void()> & operation)
{
  QElapsedTimer clock;
  clock.start();
  operation();
  QCoreApplication::processEvents();
  return static_cast<double>(clock.nsecsElapsed()) / 1.0e6;
}
/// Печатает устойчивые статистики 30 замеров и сообщает о превышении заданного предела.
bool report(const char * name, std::vector<double> values, double limit, bool maximum = false)
{
  std::sort(values.begin(), values.end());
  const double p95 = values[(values.size() * 95 + 99) / 100 - 1];
  const char * prefix = "{\"operation\":\"";
  const char * sampleKey = "\",\"samples\":";
  std::cout << prefix << name << sampleKey << values.size()
            << ",\"medianMs\":" << (values[values.size() / 2 - 1] + values[values.size() / 2]) / 2 << ",\"p95Ms\":" << p95
            << ",\"maxMs\":" << values.back() << ",\"limitMs\":" << limit << "}\n";
  return (maximum ? values.back() : p95) <= limit;
}
} // namespace
/// Измеряет завершённые операции настоящего окна без утверждений о физическом выводе кадра.
int performance(const QString & root, int segments)
{
  try
  {
    if (segments != 500 && segments != 1000)
      throw std::runtime_error("Ожидалось 500 или 1000 сегментов");
    const auto source = largeDocument(segments);
    PolygonDraft draft;
    draft.document = source;
    draft.metadata.savedAtUtc = "2026-10-07T00:00:00Z";
    const QString file = root + "/large.aipdraft.json";
    std::string error;
    if (!savePolygonDraftToFile(file.toStdString(), draft, error))
      throw std::runtime_error(error);
    Desktop app(root);
    std::vector<double> display;
    for (int index = -2; index < 30; ++index)
    {
      const double duration = elapsed(
        [&]()
        {
          app.open(file);
          app.window.grab();
        });
      if (!app.workspace->snapshot().documentValid)
        throw std::runtime_error("Измерительная задача не прошла точную проверку");
      if (index >= 0)
        display.push_back(duration);
    }
    PolygonCanvasSpatialIndex spatial;
    spatial.rebuild(app.workspace->snapshot().editableDocument);
    auto * tabs = widget<QTabWidget>(app.window, "workspaceRightTabs");
    click(tabs->tabBar(), tabs->tabBar()->tabRect(0).center());
    auto * canvas = widget<PolygonCanvasWidget>(app.window, "polygonCanvas");
    QImage image(canvas->size(), QImage::Format_ARGB32_Premultiplied);
    std::vector<double> hit, snap, render, preview, edit, undo, redo, enqueue, write;
    const auto part = source.parts.front().id;
    for (int index = -2; index < 30; ++index)
    {
      const auto sample = [&](std::vector<double> & values, const std::function<void()> & operation)
      {
        const double duration = elapsed(operation);
        if (index >= 0)
          values.push_back(duration);
      };
      sample(hit, [&]() { static_cast<void>(spatial.find({70, 40}, 1, {}, part)); });
      sample(snap, [&]() { static_cast<void>(spatial.snap({70.1, 40}, 1, {}, part, {})); });
      sample(render, [&]() { canvas->render(&image); });
      auto * tree = widget<QTreeWidget>(app.window, "editorEntityTree");
      tree->scrollToItem(tree->topLevelItem(0), QAbstractItemView::PositionAtTop);
      QCoreApplication::processEvents();
      click(tree->viewport(), tree->visualItemRect(tree->topLevelItem(0)).center());
      action(app.window, "editorHoleTool");
      click(canvas, canvas->rect().center());
      sample(preview,
             [&]()
             {
               const QPoint cursor = canvas->rect().center() + QPoint(10, 10);
               QMouseEvent move(QEvent::MouseMove, cursor, canvas->mapToGlobal(cursor), Qt::NoButton, Qt::NoButton,
                                Qt::NoModifier);
               QApplication::sendEvent(canvas, &move);
               canvas->render(&image);
             });
      key(canvas, Qt::Key_Escape);
      const double originalWidth = app.workspace->snapshot().editableDocument->sheet.width;
      sample(edit, [&]() { app.changeWidth(2100 + index); });
      if (app.workspace->snapshot().editableDocument->sheet.width != 2100 + index)
        throw std::runtime_error("Измеренная редакция не изменила документ");
      sample(undo, [&]() { action(app.window, "undoDocumentAction"); });
      if (app.workspace->snapshot().editableDocument->sheet.width != originalWidth)
        throw std::runtime_error("Измеренная отмена не восстановила документ");
      sample(redo, [&]() { action(app.window, "redoDocumentAction"); });
      if (app.workspace->snapshot().editableDocument->sheet.width != 2100 + index)
        throw std::runtime_error("Измеренный повтор не восстановил редакцию");
      // Принудительно истекает только задержка тестового таймера; запись и её очередь остаются настоящими.
      auto * timer = widget<QTimer>(app.window, "polygonAutosaveTimer");
      timer->stop();
      sample(enqueue, [&]() { QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection); });
      sample(write,
             [&]()
             {
               auto completion = std::async(std::launch::async, [&]() { app.drafts->flush(); });
               if (!waitUntil([&]() { return completion.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; }))
                 throw std::runtime_error("Не завершилась запись измерительного черновика");
               completion.get();
             });
      PolygonDraft written;
      if (!loadPolygonDraftFromFile((root + "/active.aipdraft.json").toStdString(), written, error) ||
          written.document.sheet.width != 2100 + index)
        throw std::runtime_error("Записанное поколение не соответствует измеренной редакции");
      timer->stop();
    }
    bool ok = report("display", display, 2000, true);
    ok = report("hit", hit, 16) && ok;
    ok = report("snap", snap, 16) && ok;
    ok = report("render", render, 33) && ok;
    ok = report("preview", preview, 33) && ok;
    ok = report("edit", edit, 250) && ok;
    ok = report("undo", undo, 250) && ok;
    ok = report("redo", redo, 250) && ok;
    ok = report("enqueue", enqueue, 16, true) && ok;
    ok = report("write", write, 2000, true) && ok;
    return ok ? 0 : 1;
  }
  catch (const std::exception & error)
  {
    std::cerr << "Измерение GUI не завершено: " << error.what() << '\n';
    return 2;
  }
}
} // namespace acceptance
