// sessionwidget.h —— 单个 codearts TUI 会话视图（FR-1/FR-3/FR-4）
// 封装 QTermWidget 终端，管理一个独立 PTY 子进程。
#pragma once

#include <QElapsedTimer>
#include <QWidget>
#include <qtermwidget.h>

#include "environment.h"

class QVBoxLayout;

namespace codearts {

class SessionWidget : public QWidget {
  Q_OBJECT
public:
  explicit SessionWidget(const SessionLaunch &launch,
                         QWidget *parent = nullptr);
  ~SessionWidget() override;

  bool
  start(); // 启动 TUI（FR-1），空可执行路径时返回 false（禁止静默回退 shell）
  void restart(); // 杀旧启新（FR-4 L4）

  // ── /goal 驱动通路（T15 接线）──
  void sendText(const QString &text); // 注入输入到 TUI（含 \r 提交）

  qint64 pid() const; // TUI 子进程 PID（FR-5）
  bool isRunning() const;
  qint64 uptimeMs() const; // 运行时长（FR-5）
  QString title() const;   // 标签标题

signals:
  void
  outputReceived(const QString &raw); // 原始终端输出（fromLatin1 未规范化）
  void processExited(SessionWidget *self); // 子进程退出（FR-3）

private:
  QTermWidget *createTerm(); // 创建并配置终端（回滚上限等防卡顿 FR-4 L3）
  void teardown(); // 停止并销毁终端子进程

  SessionLaunch m_launch;
  QTermWidget *m_term = nullptr;
  QVBoxLayout *m_layout = nullptr;
  QElapsedTimer m_uptime;
  bool m_running = false;
  int m_index = 0; // 会话序号（标题用）
  static int s_nextIndex;
};

} // namespace codearts
