// main.cpp —— codeartsGUI 入口
#include <QApplication>
#include <QByteArray>
#include <QtGlobal>

#ifdef CODEARTS_STATIC_IBUS_PLUGIN
#include <QtPlugin>
// 静态 Qt6 的 ibus 输入法插件（vcpkg 编成 .a 需显式导入）：编辑器/终端中文输入必需。
Q_IMPORT_PLUGIN(QIbusPlatformInputContextPlugin)
#endif

#include "mainwindow.h"

int main(int argc, char *argv[]) {
  // 默认输入法模块 ibus（与桌面环境一致；环境中已显式设置则不覆盖）。
  // 必须在构造 QApplication 前设置——平台集成在构造时读取 QT_IM_MODULE。
  if (qgetenv("QT_IM_MODULE").isEmpty())
    qputenv("QT_IM_MODULE", QByteArray("ibus"));
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("CodeArts 工作台"));
  QApplication::setOrganizationName(QStringLiteral("codeartsGUI"));
  QApplication::setApplicationVersion(QStringLiteral("0.2.5"));

  codearts::MainWindow w;
  w.show();
  return app.exec();
}
