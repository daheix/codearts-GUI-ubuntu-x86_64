// retryengine.h —— 模型错误永久重试引擎（纯逻辑，fake 时钟注入，可单测）
// FR-9：任何模型错误立即重试；第 1 次立即、连续失败间隔 60s 封顶、
// 次数无上限永久重试永不放弃；连续 ≥10 次黄条警示（继续重试不打断）。
#pragma once

#include <QString>
#include <QStringList>

namespace codearts {

// 单次重试决策与审计。时间由调用方注入（毫秒），不自取时钟——便于确定性单测。
class RetryEngine {
public:
  // ── 写死参数（FR-9，用户明确要求，不可配置）──
  static constexpr qint64 kFirstRetryImmediateMs = 0; // 第 1 次失败：立即
  static constexpr qint64 kRetryIntervalMs =
      60 * 1000;                            // 连续失败后间隔（封顶）
  static constexpr int kWarnThreshold = 10; // ≥10 次黄条警示

  // 错误发生（通道1 错误模式 / 通道2 停滞指纹共用入口）。
  // 返回 true 表示应立即重试；false 表示等待至 nextRetryAtMs()。
  bool notifyError(qint64 nowMs, const QString &kind);

  // 成功恢复（收到有效新输出）：清零计数与审计段。
  void notifySuccess();

  // 当前是否应触发重试动作（now >= nextRetryAtMs 且存在未决错误）。
  bool shouldRetry(qint64 nowMs) const;
  qint64 nextRetryAtMs() const;
  int consecutiveFailures() const; // 当前连续失败次数
  int totalRetries() const;        // 本段会话累计重试次数
  bool warningActive() const;      // 连续失败 ≥ kWarnThreshold → 黄条
  QString lastErrorKind() const;

  // 审计日志（每次错误一行：#N kind=… time=…）。
  const QStringList &auditTrail() const;

private:
  int m_consecutive = 0;
  int m_total = 0;
  qint64 m_nextRetryAtMs = -1;
  QString m_lastKind;
  QStringList m_audit;
};

} // namespace codearts
