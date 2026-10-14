// processmonitor.h —— 会话子进程健康监控（FR-5 / FR-4 L4）
// 周期采样各会话子进程 RSS(/proc/<pid>/statm) 与 uptime，超阈值置"建议重启"。
#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>

namespace codearts {

// 单会话健康快照
struct Health {
  qint64 pid = -1;
  qint64 rssBytes = 0;
  qint64 uptimeMs = 0;
  bool overMemory = false; // RSS > 阈值
  bool overUptime = false; // uptime > 阈值
};

class ProcessMonitor : public QObject {
  Q_OBJECT
public:
  explicit ProcessMonitor(QObject *parent = nullptr);

  void watch(qint64 pid, qint64 uptimeMs);
  void unwatch(qint64 pid);

  // 采样一次并发出更新（也可由定时器触发）
  void sample();

  // 供单测：读取某 pid 的 RSS（字节），失败返回 -1
  static qint64 readRssBytes(qint64 pid);

  // 阈值（默认 2GB / 60min）
  void setThresholds(qint64 maxRssBytes, qint64 maxUptimeMs);

signals:
  void healthUpdated(const QHash<qint64, Health> &all);

private:
  QTimer m_timer;
  QHash<qint64, qint64> m_uptime; // pid → uptimeMs（由外部提供）
  QHash<qint64, Health> m_last;
  qint64 m_maxRss = 2LL * 1024 * 1024 * 1024; // 2GB
  qint64 m_maxUptime = 60LL * 60 * 1000;      // 60min
};

} // namespace codearts
