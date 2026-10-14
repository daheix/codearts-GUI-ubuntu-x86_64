// sessionmanager.cpp —— 会话管理实现
#include "sessionmanager.h"

#include <QProcessEnvironment>

namespace codearts {

SessionManager::SessionManager(QObject *parent) : QObject(parent) {
  // 装配一次共享启动环境（FR-2）
  m_launch = Environment::buildSession();
  m_envValid = !m_launch.codeartsPath.isEmpty();
}

SessionWidget *SessionManager::createSession(QWidget *parentTabHost) {
  if (!m_envValid) {
    return nullptr;
  }
  auto *s = new SessionWidget(m_launch, parentTabHost);

  if (!s->start()) { // FR-1：启动 TUI；失败（空可执行）则清理并上抛
    delete s;
    return nullptr;
  }
  m_sessions.append(s);

  // 子进程退出时从集合移除（保留 widget 由上层决定是否关标签）
  connect(s, &SessionWidget::processExited, this, [this](SessionWidget *ws) {
    Q_UNUSED(ws); // widget 仍由标签页持有，仅记录状态
  });

  emit sessionCreated(s);
  return s;
}

void SessionManager::closeSession(SessionWidget *s) {
  if (!s) {
    return;
  }
  m_sessions.removeAll(s);
  emit sessionClosed(s);
  s->deleteLater();
}

void SessionManager::restartSession(SessionWidget *s) {
  if (s) {
    s->restart(); // FR-4 L4：杀旧启新，不影响其他会话
  }
}

QList<SessionWidget *> SessionManager::sessions() const { return m_sessions; }

} // namespace codearts
