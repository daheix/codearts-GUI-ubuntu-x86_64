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
  setPhase(GoalPhase::AwaitingModel);
}

void GoalRunner::start(const QString &goal, const QString &acceptance) {
  m_state = GoalState{};
  m_state.goal = goal;
  m_acceptance = acceptance;
  m_state.status = QStringLiteral("driving");
  m_retry = RetryEngine{};
  m_awaitingDone = false;
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

  // 先做错误分类（通道1）。错误输出不是"存活"信号，不得清零计数。
  const QString kind = GoalProtocol::classifyModelError(raw);
  const GoalMarkers m = GoalProtocol::parse(raw);

  if (!kind.isEmpty() && m_phase == GoalPhase::AwaitingModel) {
    // 真实模型错误 → 永久重试。
    scheduleRetry(kind);
    return;
  }

  // 非错误的真实输出 → 模型/会话存活，重置连续失败计数（保留审计）。
  if (m_retry.consecutiveFailures() > 0)
    m_retry.notifySuccess();

  handleMarkers(m);
}

void GoalRunner::handleMarkers(const GoalMarkers &m) {
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

void GoalRunner::onSessionDied() {
  if (m_phase == GoalPhase::Done || m_phase == GoalPhase::Idle)
    return;
  setPhase(GoalPhase::Recovering);
  emit statusText(QStringLiteral("会话中断，重启并断点续跑（永久续航）…"));
  // 由 UI 层重建会话后调用 onSessionDied 完成后的续跑；此处仅记录与上报。
  // UI 重建完成后应调用 injectResume（经由再次 onSessionDied 的恢复分支）。
  persist();
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
    setPhase(GoalPhase::AwaitingModel);
    emit statusText(QStringLiteral("验收未通过，继续驱动"));
  }
}

} // namespace codearts
