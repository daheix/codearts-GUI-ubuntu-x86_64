// thememanager.h —— 双主题（浅色/深色，默认深色）FR-12
// QSS + QPalette 双管齐下；QSettings 持久化；qtermwidget 终端配色跟随。
#pragma once

#include <QPalette>
#include <QString>

class QApplication;

namespace codearts {

enum class Theme { Dark, Light };

class ThemeManager {
public:
  // QSettings("codeartsGUI","codeartsGUI") 持久化；无键时默认 Dark（FR-12）。
  static Theme current();
  static void set(Theme t);

  // 应用到全局（QSS + QPalette）。root 供后续扩展（如终端控件单独覆盖）。
  static void apply(QApplication &app, Theme t);

  // 纯函数（可单测）：生成 QSS 与调色板。
  static QString styleSheet(Theme t);
  static QPalette palette(Theme t);

  // qtermwidget 配色方案名（其内置 ColorScheme 键）。
  static QString terminalScheme(Theme t);
};

} // namespace codearts
