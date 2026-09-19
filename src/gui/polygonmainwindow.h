#ifndef AIPACKAGING_GUI_POLYGONMAINWINDOW_H
#define AIPACKAGING_GUI_POLYGONMAINWINDOW_H

#include <QMainWindow>

#include <polygonworkspaceview.h>

class PolygonWorkspaceWidget;

/// Показывает только полигональный раскрой и передаёт действия прикладному контроллеру.
class PolygonMainWindow final : public QMainWindow,
                                public IPolygonWorkspaceOutput
{
  Q_OBJECT
public:
  /// Создаёт полигональную рабочую область без унаследованного клеточного редактора.
  explicit PolygonMainWindow(QWidget * parent = nullptr);

  /// Подключает действия контроллера и восстанавливает ранее проверенный путь модели.
  void setPolygonWorkspaceActions(PolygonWorkspaceActions actions);

  /// Отображает согласованный снимок состояния полигональной рабочей области.
  void presentPolygonWorkspace(const PolygonWorkspaceSnapshot & snapshot) override;

private:
  PolygonWorkspaceWidget * workspace_;
  PolygonWorkspaceActions actions_;
};

#endif
