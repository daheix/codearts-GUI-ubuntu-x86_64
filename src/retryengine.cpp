// retryengine.cpp —— 永久重试引擎实现
#include "retryengine.h"

namespace codearts {

bool RetryEngine::notifyError(qint64 nowMs, const QString &kind) {
  ++m_consecutive;
  ++m_total;
  m_lastKind = kind;
  // 第 1 次立即；之后 60s 封顶（不随次数增长）
  m_nextRetryAtMs = (m_consecutive == 1) ? nowMs + kFirstRetryImmediateMs
                                         : nowMs + kRetryIntervalMs;
  m_audit.append(
      QStringLiteral("#%1 kind=%2 time=%3").arg(m_total).arg(kind).arg(nowMs));
  return shouldRetry(nowMs);
}

void RetryEngine::notifySuccess() {
  m_consecutive = 0;
  m_nextRetryAtMs = -1;
  m_lastKind.clear();
  // 审计保留（永久留痕供 UI 展示），仅清活跃状态。
}

bool RetryEngine::shouldRetry(qint64 nowMs) const {
  // 永久重试：只要存在未决错误即永远到期可重试（无次数上限、无放弃路径）。
  return m_nextRetryAtMs >= 0 && nowMs >= m_nextRetryAtMs;
}

qint64 RetryEngine::nextRetryAtMs() const { return m_nextRetryAtMs; }

int RetryEngine::consecutiveFailures() const { return m_consecutive; }

int RetryEngine::totalRetries() const { return m_total; }

bool RetryEngine::warningActive() const {
  return m_consecutive >= kWarnThreshold;
}

QString RetryEngine::lastErrorKind() const { return m_lastKind; }

const QStringList &RetryEngine::auditTrail() const { return m_audit; }

} // namespace codearts
