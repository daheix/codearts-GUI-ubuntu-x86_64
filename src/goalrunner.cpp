// goalrunner.cpp —— /goal 自愈目标驱动状态机实现
#include "goalrunner.h"

#include <QDateTime>

namespace codearts {

GoalRunner::GoalRunner(ITerminalSink *sink, QString statePath, QObject *parent)
    : QObject(parent), m_sink(sink), m_statePath(std::move(statePath)) {}

qint64 GoalRunner::nowMs() const {
  return m_nowMs >= 0 ? m_nowMs : QDateTime::currentMSecsSinceEpoch();
}

void GoalRunner::setClock(qint64 nowMs) { m_nowMs = nowMs; }

GoalPhase GoalRunner::phase() const { return m_phase; }
const GoalState &GoalRunner::state() const { return m_state; }
const RetryEngine &GoalRunner::retry() const { return m_retry; }
bool GoalRunner::warningActive() const { return m_retry.warningActive(); }

void GoalRunner::setPhase(GoalPhase p) {
  if (m_phase == p)
    return;
  m_phase = p;
  emit phaseChanged(p);
}

void GoalRunner::persist() {
  QString err;
  if (!m_state.save(m_statePath, &err)) {
    // 断点落盘失败是显式错误：上报状态栏，不静默（无兜底）。
    emit statusText(QStringLiteral("断点保存失败: %1").arg(err));
  }
}

void GoalRunner::injectContract() {
  const QString prompt =
      GoalProtocol::contractPrompt(m_state.goal, m_acceptance);
  m_state.status = QStringLiteral("driving");
  persist();
  emit injectionRequested(prompt);
  if (m_sink)
    m_sink->inject(prompt + QStringLiteral("\r"));
  m_lastFeedMs = m_lastProgressMs =
      nowMs(); // 注入完毕开始计时，等待模型/新输出
  setPhase(GoalPhase::AwaitingModel);
}

void GoalRunner::injectResume() {
  const QString prompt =
      GoalProtocol::resumePrompt(m_state.goal, m_state.doneSteps(),
                                 m_state.totalSteps, m_state.lastStepTitle);
  m_state.status = QStringLiteral("driving");
  persist();
  emit injectionRequested(prompt);
  if (m_sink)
    m_sink->inject(prompt + QStringLiteral("\r"));
  m_lastFeedMs = m_lastProgressMs =
      nowMs(); // 注入完毕开始计时，等待模型/新输出
  setPhase(GoalPhase::AwaitingModel);
}

void GoalRunner::start(const QString &goal, const QString &acceptance) {
  m_state = GoalState{};
  m_state.goal = goal;
  m_acceptance = acceptance;
  m_state.status = QStringLiteral("driving");
  m_retry = RetryEngine{};
  m_awaitingDone = false;
  m_recoverSinceMs = -1;
  m_lastFeedMs = m_lastProgressMs = nowMs();
  // 先落盘（含目标与验收），再下发契约——崩溃亦可恢复。
  persist();
  emit statusText(QStringLiteral("下发目标契约…"));
  injectContract();
}

void GoalRunner::stop() {
  m_state.status = QStringLiteral("idle");
  persist();
  setPhase(GoalPhase::Idle);
  emit statusText(QStringLiteral("/goal 已停止"));
}

void GoalRunner::feed(const QString &raw) {
  m_lastFeedMs = nowMs();
  const GoalMarkers m = GoalProtocol::parse(raw);

  // 进度优先：同段既有步进/完成标记，就不按错误处理（避免把含"timeout"字样的
  // 步骤描述误判为模型错误）。
  if (m.hasStep || m.hasDone) {
    if (m_retry.consecutiveFailures() > 0)
      m_retry.notifySuccess();
    handleMarkers(m);
    return;
  }

  // 错误检测（通道1）：放宽到所有"活动阶段"，Driving/Reccovering 出错同样续航。
  const bool activePhase = m_phase == GoalPhase::AwaitingModel ||
                           m_phase == GoalPhase::Driving ||
                           m_phase == GoalPhase::Recovering;
  const QString kind = GoalProtocol::classifyModelError(raw);
  if (!kind.isEmpty() && activePhase) {
    scheduleRetry(kind);
    return;
  }

  // 普通输出：会话存活，重置连续失败计数（保留审计）。
  if (m_retry.consecutiveFailures() > 0)
    m_retry.notifySuccess();
}

void GoalRunner::handleMarkers(const GoalMarkers &m) {
  if (m.hasStep || m.hasDone)
    m_lastProgressMs = nowMs(); // 有真实步进/完成 → 刷新无进度计时
  if (m.hasPlan || m.hasStep) {
    setPhase(GoalPhase::Driving);
  }
  if (m.hasStep) {
    // 记录进度：把当前步骤及之前标 done。
    if (m_state.totalSteps < m.stepTotal)
      m_state.totalSteps = m.stepTotal;
    // 确保 steps 容器够长
    while (m_state.steps.size() < m.stepTotal) {
      GoalStep st;
      st.index = m_state.steps.size() + 1;
      m_state.steps.append(st);
    }
    for (int i = 0; i < m.stepCurrent - 1 && i < m_state.steps.size(); ++i)
      m_state.steps[i].done = true;
    if (m.stepCurrent >= 1 && m.stepCurrent <= m_state.steps.size()) {
      auto &cur = m_state.steps[m.stepCurrent - 1];
      cur.title = m.stepTitle;
      cur.recommendation = m.recommendation;
      // 步骤标记出现即视为该步推进；当前步在下一步出现时转 done。
      m_state.lastStepTitle = m.stepTitle;
    }
    persist();
    emit progress(m_state.doneSteps(), m_state.totalSteps, m.stepTitle);
  }
  if (m.hasDone) {
    // AI 自报完成 → 进入判据核验（双过才算 100%）。
    m_awaitingDone = true;
    m_state.status = QStringLiteral("verifying");
    persist();
    setPhase(GoalPhase::Verifying);
    emit statusText(QStringLiteral("AI 申报完成，执行验收命令核验…"));
    emit verifyRequested(m_acceptance);
  }
}

// ── 看门狗写死参数（用户指定：1 分钟轮询一次对话状态）──
static constexpr qint64 kWatchdogSilenceMs =
    60 * 1000; // 目标进行中持续 60s 无任何终端输出→判停滞
static constexpr qint64 kWatchdogNoProgressMs =
    180 * 1000; // AwaitingModel 下 180s 无步进→判卡
static constexpr qint64 kRecoverResumeMs =
    90 * 1000; // 会话重建后 90s 内强制续跑（兜底）
static constexpr qint64 kNoBaseline = 1LL << 62; // 无基线时的哨兵

void GoalRunner::scheduleRetry(const QString &kind) {
  // 永久重试：立即或到期重发当前指令。次数无上限。
  const bool immediate = m_retry.notifyError(nowMs(), kind);
  emit retrying(m_retry.consecutiveFailures(), kind);
  emit statusText(QStringLiteral("模型错误(%1)，第 %2 次重试（永久续航）")
                      .arg(kind)
                      .arg(m_retry.consecutiveFailures()));
  if (immediate) {
    pumpRetry();
  } else {
    // 到期重试由 UI 层定时调用 pumpRetry（这里不自建定时器，保持可测）。
  }
}

void GoalRunner::pumpRetry() {
  if (!m_retry.shouldRetry(nowMs()))
    return;
  // 重发：有断点进度则用恢复注入，否则重发契约。
  if (m_state.doneSteps() > 0 || m_state.totalSteps > 0) {
    injectResume();
  } else {
    injectContract();
  }
}

void GoalRunner::forceRestart(const QString &reason) {
  // 看门狗判定"非正常"后的一次强制续跑。计入连续失败/审计（黄条依据），
  // 但直接注入、不受重试间隔节流——每次触发都必须真的发出消息，绝不空转。
  m_retry.notifyError(nowMs(), reason);
  emit retrying(m_retry.consecutiveFailures(), reason);
  emit statusText(
      QStringLiteral("对话停滞/异常(%1)，强制重启续跑 #%2（永久续航）")
          .arg(reason)
          .arg(m_retry.consecutiveFailures()));
  m_lastFeedMs = m_lastProgressMs = nowMs();
  if (m_state.doneSteps() > 0 || m_state.totalSteps > 0)
    injectResume();
  else
    injectContract();
}

void GoalRunner::tick(qint64 nowMs) {
  setClock(nowMs);
  const qint64 now = nowMs;

  if (m_phase == GoalPhase::Driving || m_phase == GoalPhase::AwaitingModel ||
      m_phase == GoalPhase::Recovering) {
    // 已有错误/重试在进行：交给重试引擎按 60s 节流重发，看门狗不重复触发。
    if (m_retry.consecutiveFailures() > 0) {
      pumpRetry();
      return;
    }
  }

  if (m_phase == GoalPhase::Driving || m_phase == GoalPhase::AwaitingModel) {
    const qint64 silent =
        (m_lastFeedMs < 0) ? kNoBaseline : (now - m_lastFeedMs);
    const qint64 noProg =
        (m_lastProgressMs < 0) ? kNoBaseline : (now - m_lastProgressMs);
    if (silent >= kWatchdogSilenceMs) {
      forceRestart(QStringLiteral("stall-no-output"));
    } else if (m_phase == GoalPhase::AwaitingModel &&
               noProg >= kWatchdogNoProgressMs) {
      forceRestart(QStringLiteral("stall-no-progress"));
    }
  } else if (m_phase == GoalPhase::Recovering) {
    const qint64 silent =
        (m_lastFeedMs < 0) ? kNoBaseline : (now - m_lastFeedMs);
    const qint64 sinceDeath =
        (m_recoverSinceMs < 0) ? kNoBaseline : (now - m_recoverSinceMs);
    if (silent >= kWatchdogSilenceMs || sinceDeath >= kRecoverResumeMs) {
      forceRestart(QStringLiteral("session-recovered"));
    }
  }
}

void GoalRunner::onSessionDied() {
  if (m_phase == GoalPhase::Done || m_phase == GoalPhase::Idle)
    return;
  setPhase(GoalPhase::Recovering);
  m_recoverSinceMs = nowMs();
  m_lastFeedMs = -1; // 会话重启后以新输出重算静默
  m_lastProgressMs = nowMs();
  emit statusText(QStringLiteral("会话中断，重启并断点续跑（永久续航）…"));
  persist();
  // 续跑注入由 tick 看门狗完成：UI 重建会话后，新 TUI 就绪即静默→60s 兜底，
  // 或超过 kRecoverResumeMs(90s) 强制续跑，保证不因重建时序而卡死。
}

void GoalRunner::onVerifyFinished(bool passed, QString detail) {
  if (passed) {
    // AI 自报 [GOAL-DONE] + 判据通过 → 双过 → 100% 完成。
    m_state.status = QStringLiteral("done");
    for (auto &s : m_state.steps)
      s.done = true;
    persist();
    setPhase(GoalPhase::Done);
    emit statusText(QStringLiteral("/goal 完成：%1").arg(detail));
    emit completed(detail);
  } else {
    // 判据不过 → 注入失败原因，继续驱动（不放弃）。
    m_state.status = QStringLiteral("driving");
    m_awaitingDone = false;
    persist();
    const QString refeed =
        QStringLiteral("验收未通过：%1\n请分析失败原因，修正后继续执行，"
                       "完成后再次输出 [GOAL-DONE]。")
            .arg(detail);
    emit injectionRequested(refeed);
    if (m_sink)
      m_sink->inject(refeed + QStringLiteral("\r"));
    m_lastFeedMs = m_lastProgressMs = nowMs();
    setPhase(GoalPhase::AwaitingModel);
    emit statusText(QStringLiteral("验收未通过，继续驱动"));
  }
}

} // namespace codearts
