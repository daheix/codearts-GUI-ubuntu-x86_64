// sessionwidget.cpp —— 单会话视图实现
#include "sessionwidget.h"

#include <QVBoxLayout>

#include <csignal>
#include <sys/types.h>

namespace codearts {

int SessionWidget::s_nextIndex = 1;

SessionWidget::SessionWidget(const SessionLaunch &launch, QWidget *parent)
    : QWidget(parent), m_launch(launch), m_index(s_nextIndex++) {
  m_layout = new QVBoxLayout(this);
  m_layout->setContentsMargins(0, 0, 0, 0);
  m_layout->setSpacing(0);
}

SessionWidget::~SessionWidget() { teardown(); }

QTermWidget *SessionWidget::createTerm() {
  // startnow=0：不自动启动会话，待 start() 里 setShellProgram 后手动启动，
  // 否则 qtermwidget 会在构造时用 $SHELL 提前启动（掩盖真实程序）。
  auto *term = new QTermWidget(0, this);
  // FR-4 L3：限制回滚缓冲行数，防长会话内存无限膨胀
  term->setHistorySize(5000);
  term->setScrollBarPosition(QTermWidget::ScrollBarRight);
  // 子进程退出 → 更新状态并上抛（FR-3/FR-4）。finished() 是公开信号。
  connect(term, &QTermWidget::finished, this, [this]() {
    m_running = false;
    emit processExited(this);
  });
  // 原始输出上抛（/goal 驱动用，T15）。receivedData 为 fromLatin1 产物，
  // 交由 GoalRunner 用 GoalProtocol::normalizeReceived 规范化后再解析。
  connect(term, &QTermWidget::receivedData, this,
          [this](const QString &data) { emit outputReceived(data); });
  m_layout->addWidget(term);
  return term;
}

bool SessionWidget::start() {
  if (m_running) {
    return true;
  }
  // 显式拒绝空可执行路径：否则 qtermwidget 会静默回退到 $SHELL(bash)，
  // 用错误行为掩盖"未找到 codearts"的真实失败（违反"禁止 fallback"铁律）。
  if (m_launch.codeartsPath.isEmpty()) {
    return false;
  }
  m_term = createTerm();

  // 装配环境：完整 systemEnvironment + OPENCODE_* 增量（"KEY=VALUE" 列表）
  QStringList envList;
  const auto entries = m_launch.env.keys();
  envList.reserve(entries.size());
  for (const QString &key : entries) {
    envList.append(key + QLatin1Char('=') + m_launch.env.value(key));
  }
  m_term->setEnvironment(envList);
  m_term->setShellProgram(m_launch.codeartsPath);
  m_term->startShellProgram();

  m_running = true;
  m_uptime.start();
  return true;
}

void SessionWidget::restart() {
  // 销毁旧终端再重建：子进程死亡是异步的，复用旧 session 会导致
  // startShellProgram 因 isRunning() 仍为真而静默失效。重建保证重启可靠（FR-4
  // L4）。
  teardown();
  start();
}

void SessionWidget::sendText(const QString &text) {
  if (m_term) {
    m_term->sendText(text);
  }
}

void SessionWidget::teardown() {
  if (m_term) {
    if (m_running) {
      // 终止前台子进程（codearts TUI），qtermwidget 负责回收
      const qint64 pid = m_term->getForegroundProcessId();
      if (pid > 0) {
        ::kill(static_cast<pid_t>(pid), SIGTERM);
      }
    }
    m_layout->removeWidget(m_term);
    m_term->deleteLater(); // 由 Qt 事件循环安全销毁（含其 PTY 会话）
    m_term = nullptr;
  }
  m_running = false;
}

qint64 SessionWidget::pid() const {
  return m_term ? m_term->getForegroundProcessId() : -1;
}

bool SessionWidget::isRunning() const { return m_running; }

qint64 SessionWidget::uptimeMs() const {
  return m_running ? m_uptime.elapsed() : 0;
}

QString SessionWidget::title() const {
  return QStringLiteral("会话 %1").arg(m_index);
}

} // namespace codearts
