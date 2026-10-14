// goalrunner.cpp —— /goal 自愈目标驱动状态机实现
#include "goalrunner.h"

#include <QDateTime>
#include <QRegularExpression>

namespace codearts {

// 前向声明（定义见下方看门狗参数块，内部链接）
static bool hasRealContent(const QString &raw);
static bool reportsBusy(const QString &raw);

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
  m_lastFeedMs = m_lastProgressMs = m_lastContentMs =
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
  m_lastFeedMs = m_lastProgressMs = m_lastContentMs =
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
  m_errorRetryPending = false;
  m_lastBusySignalMs = -1; // 等 TUI 首个 interrupt 信号
  m_lastFeedMs = m_lastProgressMs = m_lastContentMs = nowMs();
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
  if (hasRealContent(raw))
    m_lastContentMs = nowMs(); // 抗心跳：真实文字才算"活"，转轮/心跳不算
  if (reportsBusy(raw))
    m_lastBusySignalMs =
        nowMs(); // 状态栏"esc interrupt"在=模型正在工作（对话状态真值）
  const GoalMarkers m = GoalProtocol::parse(raw);

  // 进度优先：同段既有步进/完成标记，就不按错误处理（避免把含"timeout"字样的
  // 步骤描述误判为模型错误）。
  if (m.hasStep || m.hasDone) {
    if (m_retry.consecutiveFailures() > 0)
      m_retry.notifySuccess();
    m_errorRetryPending = false;
    handleMarkers(m);
    return;
  }

  // 错误检测（通道1）：活动阶段（含 Driving）出错同样续航。
  const bool activePhase = m_phase == GoalPhase::AwaitingModel ||
                           m_phase == GoalPhase::Driving ||
                           m_phase == GoalPhase::Recovering;
  const QString kind = GoalProtocol::classifyModelError(raw);
  if (!kind.isEmpty() && activePhase) {
    scheduleRetry(kind);
    return;
  }

  // 普通输出：会话存活，重置连续失败计数、清错误重试未决（保留审计）。
  if (m_retry.consecutiveFailures() > 0)
    m_retry.notifySuccess();
  m_errorRetryPending = false;
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
static constexpr qint64 kWatchdogContentMs =
    60 * 1000; // 60s 无真实文字内容→判停滞（抗心跳：忽略纯控制/转轮帧）
static constexpr qint64 kWatchdogNoProgressMs =
    60 * 1000; // 60s(用户指定 1 分钟轮询)无 [GOAL-STEP] 推进→判卡
static constexpr qint64 kWorkingGoneMs =
    20 * 1000; // 状态栏"esc interrupt"消失持续 20s→认定对话已停止
static constexpr qint64 kRecoverResumeMs =
    90 * 1000; // 会话重建后 90s 内强制续跑（兜底）
static constexpr qint64 kNoBaseline = 1LL << 62; // 无基线时的哨兵

// 判断一段原始输出是否含"真实文字内容"（剥离转义后的字母/数字/中日韩）。
// 目的：区分"模型在说话/有文字"与"纯转轮动画/光标闪烁/connection-monitor
// 心跳"。
static bool hasRealContent(const QString &raw) {
  // 逐字符判"真实文字"：字母/数字或 CJK。不用正则 Unicode
  // 转义（QRegularExpression 对 \uXXXX
  // 支持缺位曾致正则恒失效、内容判据形同虚设的坑）。转轮/方框/盲文字符属
  // 符号类，isLetterOrNumber=false 天然不计入——心跳/动画因此不刷新内容时间。
  const QString t = GoalProtocol::stripAnsi(raw);
  for (const QChar c : t) {
    if (c.isLetterOrNumber())
      return true;
    const ushort u = c.unicode();
    if (u >= 0x4E00 && u <= 0x9FFF) // CJK 统一表意
      return true;
  }
  return false;
}

// 状态栏是否显示"工作中"标志（TUI 忙时渲染 esc interrupt 字样，停时消失）。
//  词边界且只匹配独立 interrupt，排除 interrupted/interruptible。
static bool reportsBusy(const QString &raw) {
  static const QRegularExpression kBusy(
      QStringLiteral("\\binterrupt\\b"),
      QRegularExpression::CaseInsensitiveOption);
  return kBusy.match(GoalProtocol::stripAnsi(raw)).hasMatch();
}

void GoalRunner::scheduleRetry(const QString &kind) {
  // 永久重试：立即或到期重发当前指令。次数无上限。
  const bool immediate = m_retry.notifyError(nowMs(), kind);
  emit retrying(m_retry.consecutiveFailures(), kind);
  emit statusText(QStringLiteral("模型错误(%1)，第 %2 次重试（永久续航）")
                      .arg(kind)
                      .arg(m_retry.consecutiveFailures()));
  if (immediate) {
    m_errorRetryPending = false;
    pumpRetry();
  } else {
    // 记未决；到期由 UI 层 tick() 调 pumpRetry（不自建定时器，保持可测）。
    m_errorRetryPending = true;
  }
}

void GoalRunner::pumpRetry() {
  if (!m_retry.shouldRetry(nowMs()))
    return;
  m_errorRetryPending = false;
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
  m_lastFeedMs = m_lastProgressMs = m_lastContentMs = nowMs();
  if (m_state.doneSteps() > 0 || m_state.totalSteps > 0)
    injectResume();
  else
    injectContract();
}

void GoalRunner::tick(qint64 nowMs) {
  setClock(nowMs);
  const qint64 now = nowMs;

  // 1) 错误分类路径的到期重试（重试引擎 60s
  // 节流）。看门狗不与之抢，交由它驱动。
  if (m_errorRetryPending && m_retry.shouldRetry(now)) {
    pumpRetry();
    return;
  }

  const qint64 noContent =
      (m_lastContentMs < 0) ? kNoBaseline : (now - m_lastContentMs);
  const qint64 noProg =
      (m_lastProgressMs < 0) ? kNoBaseline : (now - m_lastProgressMs);
  // 对话状态真值：TUI 状态栏忙时渲染"esc
  // interrupt"，停时消失。据此区分真忙与卡住。
  const qint64 sinceBusy =
      (m_lastBusySignalMs < 0) ? kNoBaseline : (now - m_lastBusySignalMs);
  const bool chatStopped = sinceBusy >= kWorkingGoneMs; // interrupt 已消失

  if (m_phase == GoalPhase::Driving || m_phase == GoalPhase::AwaitingModel) {
    if (noProg >= kWatchdogNoProgressMs &&
        (chatStopped || noContent >= kWatchdogContentMs)) {
      // 判据：无 [GOAL-STEP] 推进达阈值，且(对话已停 或 长时间无真实内容)。
      // interrupt 在(模型仍忙)时 chatStopped=false → 绝不打断合法长步骤。
      forceRestart(chatStopped ? QStringLiteral("stall-chat-stopped")
                               : QStringLiteral("stall-no-content"));
    }
  } else if (m_phase == GoalPhase::Recovering) {
    const qint64 silent =
        (m_lastFeedMs < 0) ? kNoBaseline : (now - m_lastFeedMs);
    const qint64 sinceDeath =
        (m_recoverSinceMs < 0) ? kNoBaseline : (now - m_recoverSinceMs);
    if (sinceDeath >= kRecoverResumeMs || silent >= kWatchdogContentMs) {
      forceRestart(QStringLiteral("session-recovered"));
    }
  }
}

void GoalRunner::onSessionDied() {
  if (m_phase == GoalPhase::Done || m_phase == GoalPhase::Idle)
    return;
  setPhase(GoalPhase::Recovering);
  m_recoverSinceMs = nowMs();
  m_errorRetryPending = false;
  m_lastBusySignalMs = -1; // 重建后以新 TUI 的 interrupt 信号为准
  m_lastFeedMs = -1;       // 会话重启后以新输出重算静默
  m_lastContentMs = -1;    // 同上，重建基线
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
