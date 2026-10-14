// mainwindow.h —— 主窗口：多会话标签 + 工具栏 + 状态栏健康（FR-3/FR-5/FR-6）
#pragma once

#include <QHash>
#include <QMainWindow>

#include "processmonitor.h"

class QTabWidget;
class QLabel;
class QToolBar;
class QActionGroup;

namespace codearts {

class SessionManager;
class SessionWidget;
class GoalRunner;
class ITerminalSink;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

protected:
  void closeEvent(QCloseEvent *event) override; // 关窗时回收所有子进程

private slots:
  void onNewSession();
  void onCloseCurrentSession();
  void onRestartCurrentSession();
  void onHealthUpdated(const QHash<qint64, Health> &all);
  void onStartGoal();       // /goal 启动（T16）
  void onApplyTheme();      // 主题切换（T18）
  void onOpenPermissions(); // 权限管理页（T17）

private:
  void buildUi();
  void buildMenus();
  void updateStatusBar();
  void showEnvError();
  void wireGoal(SessionWidget *s); // 接入 GoalRunner 到会话（T15/T16）
  SessionWidget *currentSession() const;

  QTabWidget *m_tabs = nullptr;
  QLabel *m_statusLabel = nullptr;
  QLabel *m_goalLabel = nullptr;  // 状态栏 /goal 区（T16）
  QLabel *m_retryLabel = nullptr; // 重试黄条（T16，≥10 警示）
  QToolBar *m_toolbar = nullptr;
  SessionManager *m_mgr = nullptr;
  ProcessMonitor *m_monitor = nullptr;
  QHash<qint64, QString> m_statusCache; // pid → 状态文本
  GoalRunner *m_goal = nullptr;         // 当前 /goal 驱动器
  ITerminalSink *m_goalSink = nullptr;  // 目标会话注入器
  QActionGroup *m_themeGroup = nullptr;
};

} // namespace codearts
