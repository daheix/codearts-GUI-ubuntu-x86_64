// sessionmanager.h —— 会话生命周期管理（FR-3/FR-4）
// 维护 SessionWidget 集合，提供创建/关闭/重启。
#pragma once

#include <QHash>
#include <QObject>

#include "sessionwidget.h"

namespace codearts {

class SessionManager : public QObject {
  Q_OBJECT
public:
  explicit SessionManager(QObject *parent = nullptr);

  // 创建并启动一个新会话（不嵌入 UI，由调用方 addWidget 进标签页）
  SessionWidget *createSession(QWidget *parentTabHost);

  void closeSession(SessionWidget *s);
  void restartSession(SessionWidget *s);

  QList<SessionWidget *> sessions() const;

signals:
  void sessionCreated(codearts::SessionWidget *s);
  void sessionClosed(codearts::SessionWidget *s);

private:
  SessionLaunch m_launch; // 共享的启动环境
  QList<SessionWidget *> m_sessions;
  bool m_envValid = false;
};

} // namespace codearts
