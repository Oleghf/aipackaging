#ifndef AIPACKAGING_GUI_POLYGONSTATUSPANEL_H
#define AIPACKAGING_GUI_POLYGONSTATUSPANEL_H

#include <QWidget>

#include <polygonworkspaceview.h>

class QLabel;
class QProgressBar;
class QTextEdit;

/// Показывает состояние, ход выполнения и метрики последнего результата.
class PolygonStatusPanel final : public QWidget
{
  Q_OBJECT
public:
  /// Создаёт нижнюю панель сообщений в прежнем порядке отображения.
  explicit PolygonStatusPanel(QWidget * parent = nullptr);

  /// Публикует сообщение, этап, фактическую долю выполнения и метрики.
  void present(const PolygonWorkspaceSnapshot & snapshot);

private:
  QLabel * statusLabel_;
  QLabel * stageLabel_;
  QProgressBar * progressBar_;
  QTextEdit * metricsText_;
};

#endif
