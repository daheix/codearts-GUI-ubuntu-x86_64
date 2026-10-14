// goalrunner.h —— /goal 自愈目标驱动状态机（T15，核心）
// 串接 GoalProtocol(解析) + GoalState(断点) + RetryEngine(永久重试) +
// 终端注入。 铁律：任何模型错误/中断都永久续航，绝不放弃；无未标记 fallback。
#pragma once

#include <QObject>
#include <QString>

#include "goalprotocol.h"
#include "goalstate.h"
#include "retryengine.h"

namespace codearts {

// 终端注入抽象（真实=SessionWidget::sendText；测试=mock）。
class ITerminalSink {
public:
  virtual ~ITerminalSink() = default;
  virtual void inject(const QString &text) = 0; // 含 \r 提交
};

// 驱动阶段。用于 UI 状态栏与测试断言。
enum class GoalPhase {
  Idle, // 未启动
  AwaitingReady, // 已发契约，等待 composer 就绪（spike：就绪前注入会被吞）
  Driving,       // 正常步进
  AwaitingModel, // 已注入，等模型/步骤标记（超时→重试引擎）
  Verifying,     // 收到 [GOAL-DONE]，执行验收命令核验
  Recovering,    // 会话死亡→重启+断点续跑
  Done,          // AI 自报 + 判据双过 → 100%
};

class GoalRunner : public QObject {
  Q_OBJECT
public:
  // sink 注入器；statePath 断点文件路径。二者由调用方提供，Runner 不自造。
  GoalRunner(ITerminalSink *sink, QString statePath, QObject *parent = nullptr);

  // 启动 /goal：写状态、下发契约注入。
  void start(const QString &goal, const QString &acceptance);
  // 停止（用户手动终止）。
  void stop();

  // 喂入原始终端输出（receivedData，未规范化）。内部规范化+解析+推进。
  void feed(const QString &raw);

  // 会话进程死亡 → 进入 Recovering，注入断点续跑（永久重试的一环）。
  void onSessionDied();

  // 验收命令执行结果（UI 层跑完命令回调）。passed=判据是否通过。
  void onVerifyFinished(bool passed, QString detail);

  // 时钟注入（毫秒）：默认取 QDateTime；测试可设固定值以驱动重试逻辑。
  void setClock(qint64 nowMs);
  qint64 nowMs() const;

  GoalPhase phase() const;
  const GoalState &state() const;
  const RetryEngine &retry() const;
  bool warningActive() const; // ≥10 连续失败 → 黄条

public slots:
  // 由 UI 定时器周期调用：
  //  - pumpRetry：到期重试（重试引擎的节拍入口）。
  //  - tick：看门狗 + 到期重试泵。tick 以 1 分钟窗口判"目标进行中但终端长时间
  //    无输出/无进度"为停滞，强制注入续跑（不依赖错误文案匹配，FR-9
  //    兜底续航）。
  void pumpRetry();
  void tick(qint64 nowMs);

signals:
  void phaseChanged(GoalPhase phase);
  void progress(int doneSteps, int totalSteps, QString title);
  void injectionRequested(QString text); // 供 UI 记日志/审计（实际注入走 sink）
  void retrying(int consecutive, QString kind); // 永久重试信号（黄条依据）
  void verifyRequested(QString acceptance); // 请求 UI 执行验收命令
  void completed(QString summary); // 100% 完成（AI 自报+判据双过）
  void statusText(QString text);   // 状态栏文案

private:
  void setPhase(GoalPhase p);
  void persist(); // 落盘断点
  void handleMarkers(const GoalMarkers &m);
  void scheduleRetry(const QString &kind); // 永久重试入口（永不放弃）
  void forceRestart(const QString &reason); // 会话重建后：完整断点续跑
  void nudgeContinue(const QString &reason); // 会话活着仅中断：发"继续完成剩余任务"
  void injectContract();
  void injectResume();
  void injectNudge(); // 轻量续跑注入（不重发全文）

  ITerminalSink *m_sink = nullptr;
  QString m_statePath;
  GoalState m_state;
  RetryEngine m_retry;
  GoalPhase m_phase = GoalPhase::Idle;
  QString m_acceptance;
  qint64 m_nowMs = -1;      // <0 = 用真实时钟
  qint64 m_lastFeedMs = -1; // 最近一次终端输出时间（停滞判据）
  qint64 m_lastProgressMs = -1; // 最近一次步进标记/注入时间（无进度判据）
  qint64 m_recoverSinceMs =
      -1; // 会话死亡进入 Recovering 的时刻（恢复兜底计时）
  qint64 m_lastContentMs =
      -1; // 最近"真实内容"输出时间（抗心跳：忽略纯控制/转轮帧）
  bool m_errorRetryPending =
      false; // 错误分类路径有待到期重试（tick 依此驱动 60s 泵）
  qint64 m_lastBusySignalMs =
      -1; // 状态栏"esc interrupt"(工作)信号：对话状态真值
  bool m_awaitingDone = false;
};

} // namespace codearts
