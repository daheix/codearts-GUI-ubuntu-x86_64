// processmonitor.cpp —— 健康监控实现（/proc 采样）
#include "processmonitor.h"

#include <QFile>

namespace codearts {

ProcessMonitor::ProcessMonitor(QObject *parent) : QObject(parent) {
  m_timer.setInterval(5000); // FR-5：每 5s 采样
  connect(&m_timer, &QTimer::timeout, this, &ProcessMonitor::sample);
  m_timer.start();
}

void ProcessMonitor::watch(qint64 pid, qint64 uptimeMs) {
  m_uptime.insert(pid, uptimeMs);
}

void ProcessMonitor::unwatch(qint64 pid) {
  m_uptime.remove(pid);
  m_last.remove(pid);
}

void ProcessMonitor::setThresholds(qint64 maxRssBytes, qint64 maxUptimeMs) {
  m_maxRss = maxRssBytes;
  m_maxUptime = maxUptimeMs;
}

qint64 ProcessMonitor::readRssBytes(qint64 pid) {
  // /proc/<pid>/statm: size resident shared text lib data dt（单位：页）
  QFile f(QStringLiteral("/proc/%1/statm").arg(pid));
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return -1;
  }
  const QByteArray line = f.readAll().trimmed();
  const QList<QByteArray> parts = line.split(' ');
  if (parts.size() < 2) {
    return -1;
  }
  bool ok = false;
  const qint64 pages = parts.at(1).toLongLong(&ok);
  if (!ok) {
    return -1;
  }
  const qint64 pageSize = 4096; // x86_64 常规页
  return pages * pageSize;
}

void ProcessMonitor::sample() {
  QHash<qint64, Health> all;
  for (auto it = m_uptime.begin(); it != m_uptime.end(); ++it) {
    const qint64 pid = it.key();
    Health h;
    h.pid = pid;
    h.rssBytes = readRssBytes(pid);
    h.uptimeMs = it.value();
    h.overMemory = (h.rssBytes >= 0 && h.rssBytes > m_maxRss);
    h.overUptime = (h.uptimeMs > m_maxUptime);
    all.insert(pid, h);
  }
  m_last = all;
  emit healthUpdated(all);
}

} // namespace codearts
