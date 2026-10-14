// main.cpp —— codeartsGUI 入口
#include <QApplication>

#include "mainwindow.h"

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("CodeArts 工作台"));
  QApplication::setOrganizationName(QStringLiteral("codeartsGUI"));
  QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

  codearts::MainWindow w;
  w.show();
  return app.exec();
}
