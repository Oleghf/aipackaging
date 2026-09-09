#include <QApplication>

#include <maincontroller.h>
#include <qtview.h>

int main(int argc, char * argv[])
{
  QApplication app(argc, argv);

  QtView mainWindow;
  mainWindow.show();

  std::shared_ptr<IView> view(&mainWindow, [](IView *) {});
  std::shared_ptr<MainController> mainController = std::make_shared<MainController>(view);
  view->addEventListener(mainController);

  return app.exec();
}
