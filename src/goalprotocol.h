// goalprotocol.h —— /goal 契约协议解析与注入文案构造（纯逻辑，可单测）
// FR-7：[GOAL-PLAN]/[GOAL-STEP n/N]/[GOAL-DONE] 标记解析。
// FR-9：模型错误模式匹配（六类）。
#pragma once

#include <QString>

namespace codearts {

// 解析结果：一段（已清洗的）文本中发现的协议标记
struct GoalMarkers {
  bool hasPlan = false;
  bool hasStep = false;
  bool hasDone = false;
  int stepCurrent = 0;
  int stepTotal = 0;
  QString stepTitle;      // 步骤描述（去掉标记与推荐方案）
  QString recommendation; // "| 推荐方案: ..." 内容（可空）
};

class GoalProtocol {
public:
  // receivedData 为 QString::fromLatin1 的产物：先 toLatin1 还原原始字节，再按
  // UTF-8 解码。 修复中文双重编码（spike 实测确认 Session::receivedData 用
  // fromLatin1）。
  static QString normalizeReceived(const QString &rawFromTerminal);

  // 剥离 ANSI/VT 转义序列（CSI/OSC/DCS/APC 与单字符 ESC 序列），用于标记匹配。
  static QString stripAnsi(const QString &text);

  // 解析协议标记。输入应已 normalize + stripAnsi（内部会再 strip
  // 一次以容忍嵌套）。
  static GoalMarkers parse(const QString &text);

  // 模型错误模式匹配（六类：timeout/rate_limit/server/network/empty/refusal）。
  // 命中返回类别名（小写英文），未命中返回空串。模式表写死（FR-9）。
  static QString classifyModelError(const QString &text);

  // ── 注入文案 ──
  // 契约下发（首条注入）：目标全文 + 三标记协议 +
  // 决策自主权条款（检索→自荐→直行）。
  static QString contractPrompt(const QString &goal, const QString &acceptance);

  // 断点恢复（会话重建后注入）：带已完成进度上下文。
  static QString resumePrompt(const QString &goal, int doneSteps,
                              int totalSteps, const QString &lastStepTitle);
};

} // namespace codearts
