#ifndef AIPACKAGING_GUI_POLYGONCANVASRENDERER_H
#define AIPACKAGING_GUI_POLYGONCANVASRENDERER_H

class PolygonCanvasWidget;

/// Координирует полный кадр полигонального полотна, не владея документом или состоянием взаимодействия.
class PolygonCanvasRenderer final
{
public:
  /// Рисует один кадр в закреплённом порядке слоёв через закрытые примитивы совместимого фасада.
  static void paint(PolygonCanvasWidget & canvas);
};

#endif
