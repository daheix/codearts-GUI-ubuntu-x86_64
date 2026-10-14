// thememanager.cpp —— 双主题实现（默认深色）
#include "thememanager.h"

#include <QApplication>
#include <QSettings>

namespace codearts {

static constexpr const char *kOrg = "codeartsGUI";
static constexpr const char *kApp = "codeartsGUI";
static constexpr const char *kKeyTheme = "appearance/theme";

Theme ThemeManager::current() {
  QSettings s(kOrg, kApp);
  const QString v = s.value(QString::fromLatin1(kKeyTheme)).toString();
  if (v == QLatin1String("light"))
    return Theme::Light;
  if (v == QLatin1String("dark"))
    return Theme::Dark;
  return Theme::Dark; // FR-12 默认深色
}

void ThemeManager::set(Theme t) {
  QSettings s(kOrg, kApp);
  s.setValue(QString::fromLatin1(kKeyTheme), t == Theme::Light
                                                 ? QStringLiteral("light")
                                                 : QStringLiteral("dark"));
}

QString ThemeManager::styleSheet(Theme t) {
  if (t == Theme::Light) {
    return QStringLiteral(
        "QMainWindow, QDialog { background: #f5f6f8; color: #1c1e21; }\n"
        "QWidget { color: #1c1e21; }\n"
        "QToolBar { background: #e8eaed; border-bottom: 1px solid #c9cdd4; "
        "spacing: 6px; }\n"
        "QLineEdit, QPlainTextEdit, QComboBox, QSpinBox {\n"
        "  background: #ffffff; border: 1px solid #c9cdd4; border-radius: "
        "4px;\n"
        "  padding: 4px; selection-background-color: #b3d4fc; }\n"
        "QPushButton { background: #ffffff; border: 1px solid #c9cdd4;\n"
        "  border-radius: 4px; padding: 5px 14px; }\n"
        "QPushButton:hover { background: #eaf2fd; border-color: #7aa7f0; }\n"
        "QPushButton:pressed { background: #d6e6fb; }\n"
        "QPushButton:disabled { color: #9aa0a6; background: #f0f1f3; }\n"
        "QTabWidget::pane { border: 1px solid #c9cdd4; background: #ffffff; }\n"
        "QTabBar::tab { background: #e8eaed; padding: 6px 12px;\n"
        "  border: 1px solid #c9cdd4; border-bottom: none; }\n"
        "QTabBar::tab:selected { background: #ffffff; }\n"
        "QMenuBar { background: #e8eaed; }\n"
        "QMenuBar::item:selected { background: #d6e6fb; }\n"
        "QMenu { background: #ffffff; border: 1px solid #c9cdd4; }\n"
        "QMenu::item:selected { background: #d6e6fb; }\n"
        "QStatusBar { background: #e8eaed; border-top: 1px solid #c9cdd4; }\n"
        "QTableView, QTreeView { background: #ffffff; "
        "alternate-background-color: #f5f6f8;\n"
        "  gridline-color: #dde1e6; }\n"
        "QHeaderView::section { background: #e8eaed; padding: 4px;\n"
        "  border: 1px solid #c9cdd4; }\n"
        "QProgressBar { background: #ffffff; border: 1px solid #c9cdd4;\n"
        "  border-radius: 4px; text-align: center; }\n"
        "QProgressBar::chunk { background-color: #4c8bf5; border-radius: 3px; "
        "}\n"
        "QScrollBar:vertical { width: 12px; background: #f0f1f3; }\n"
        "QScrollBar::handle:vertical { background: #c9cdd4; border-radius: "
        "5px;\n"
        "  min-height: 24px; }\n"
        "QScrollBar:horizontal { height: 12px; background: #f0f1f3; }\n"
        "QScrollBar::handle:horizontal { background: #c9cdd4; border-radius: "
        "5px;\n"
        "  min-width: 24px; }\n"
        "QToolTip { background: #ffffff; color: #1c1e21; border: 1px solid "
        "#c9cdd4; }\n");
  }
  return QStringLiteral(
      "QMainWindow, QDialog { background: #131416; color: #e6e6e6; }\n"
      "QWidget { color: #e6e6e6; }\n"
      "QToolBar { background: #1b1c1f; border-bottom: 1px solid #2b2d31; "
      "spacing: 6px; }\n"
      "QLineEdit, QPlainTextEdit, QComboBox, QSpinBox {\n"
      "  background: #1b1c1f; border: 1px solid #2b2d31; border-radius: 4px;\n"
      "  padding: 4px; selection-background-color: #264f78; }\n"
      "QPushButton { background: #2b2d31; border: 1px solid #3a3d42;\n"
      "  border-radius: 4px; padding: 5px 14px; }\n"
      "QPushButton:hover { background: #34373c; border-color: #4c8bf5; }\n"
      "QPushButton:pressed { background: #264f78; }\n"
      "QPushButton:disabled { color: #6b6f76; background: #22242a; }\n"
      "QTabWidget::pane { border: 1px solid #2b2d31; background: #131416; }\n"
      "QTabBar::tab { background: #1b1c1f; padding: 6px 12px;\n"
      "  border: 1px solid #2b2d31; border-bottom: none; }\n"
      "QTabBar::tab:selected { background: #131416; }\n"
      "QMenuBar { background: #1b1c1f; }\n"
      "QMenuBar::item:selected { background: #264f78; }\n"
      "QMenu { background: #1b1c1f; border: 1px solid #2b2d31; }\n"
      "QMenu::item:selected { background: #264f78; }\n"
      "QStatusBar { background: #1b1c1f; border-top: 1px solid #2b2d31; }\n"
      "QTableView, QTreeView { background: #131416; "
      "alternate-background-color: #1b1c1f;\n"
      "  gridline-color: #2b2d31; }\n"
      "QHeaderView::section { background: #1b1c1f; padding: 4px;\n"
      "  border: 1px solid #2b2d31; }\n"
      "QProgressBar { background: #1b1c1f; border: 1px solid #2b2d31;\n"
      "  border-radius: 4px; text-align: center; }\n"
      "QProgressBar::chunk { background-color: #4c8bf5; border-radius: 3px; }\n"
      "QScrollBar:vertical { width: 12px; background: #1b1c1f; }\n"
      "QScrollBar::handle:vertical { background: #3a3d42; border-radius: 5px;\n"
      "  min-height: 24px; }\n"
      "QScrollBar:horizontal { height: 12px; background: #1b1c1f; }\n"
      "QScrollBar::handle:horizontal { background: #3a3d42; border-radius: "
      "5px;\n"
      "  min-width: 24px; }\n"
      "QToolTip { background: #2b2d31; color: #e6e6e6; border: 1px solid "
      "#3a3d42; }\n");
}

QPalette ThemeManager::palette(Theme t) {
  QPalette p;
  if (t == Theme::Light) {
    const QColor base(0xf5, 0xf6, 0xf8);
    const QColor base2(0xff, 0xff, 0xff);
    const QColor text(0x1c, 0x1e, 0x21);
    const QColor dis(0x9a, 0xa0, 0xa6);
    const QColor hl(0xd6, 0xe6, 0xfb);
    const QColor hlt(0x1c, 0x1e, 0x21);
    p.setColor(QPalette::Window, base);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base2);
    p.setColor(QPalette::AlternateBase, base);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, base2);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Disabled, QPalette::WindowText, dis);
    p.setColor(QPalette::Disabled, QPalette::Text, dis);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, dis);
    p.setColor(QPalette::Highlight, hl);
    p.setColor(QPalette::HighlightedText, hlt);
  } else {
    const QColor base(0x13, 0x14, 0x16);
    const QColor base2(0x1b, 0x1c, 0x1f);
    const QColor text(0xe6, 0xe6, 0xe6);
    const QColor dis(0x6b, 0x6f, 0x76);
    const QColor hl(0x26, 0x4f, 0x78);
    p.setColor(QPalette::Window, base);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base2);
    p.setColor(QPalette::AlternateBase, base);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, base2);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Disabled, QPalette::WindowText, dis);
    p.setColor(QPalette::Disabled, QPalette::Text, dis);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, dis);
    p.setColor(QPalette::Highlight, hl);
    p.setColor(QPalette::HighlightedText, text);
  }
  return p;
}

QString ThemeManager::terminalScheme(Theme t) {
  // qtermwidget 内置方案名；深色默认即其原生深底方案。
  return t == Theme::Light ? QStringLiteral("BlackOnWhite")
                           : QStringLiteral("Breeze");
}

void ThemeManager::apply(QApplication &app, Theme t) {
  app.setStyleSheet(styleSheet(t));
  app.setPalette(palette(t));
}

} // namespace codearts
