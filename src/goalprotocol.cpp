// goalprotocol.cpp —— /goal 协议解析实现
#include "goalprotocol.h"

#include <QRegularExpression>

namespace codearts {

QString GoalProtocol::normalizeReceived(const QString &rawFromTerminal) {
  // receivedData 逐 Latin1 字节映射；toLatin1 无损还原原始字节，再按 UTF-8
  // 解码。
  return QString::fromUtf8(rawFromTerminal.toLatin1());
}

QString GoalProtocol::stripAnsi(const QString &text) {
  // 状态机剥离：CSI (ESC [ ... 终结字节) / OSC (ESC ] ... BEL|ST) /
  // DCS|APC|PM|SOS (ESC P/^/_/X ... ST) / 单字符 ESC 序列。
  QString out;
  out.reserve(text.size());
  int i = 0;
  const int n = text.size();
  while (i < n) {
    const QChar c = text.at(i);
    if (c != QChar(0x1b)) {
      out.append(c);
      ++i;
      continue;
    }
    // ESC 已消费：判别后续
    if (i + 1 >= n)
      break;
    const QChar k = text.at(i + 1);
    if (k == QLatin1Char('[')) {
      // CSI：ESC [ <参数 0x30-0x3F><中间 0x20-0x2F> <终结 0x40-0x7E>
      // 必须先吃参数/中间字节（含私有标记 ?、分号、数字），再吃一个终结字节，
      // 否则 ESC[?2026h 会残留 "2026h" 污染内容判定与遍历（抗心跳失效的根因）。
      i += 2;
      while (i < n) {
        const ushort ch = text.at(i).unicode();
        if (ch >= 0x20 && ch <= 0x3F) {
          ++i; // 参数/中间字节
          continue;
        }
        if (ch >= 0x40 && ch <= 0x7E) {
          ++i; // 终结字节
        }
        break;
      }
    } else if (k == QLatin1Char(']')) {
      // OSC：至 BEL 或 ESC \（ST）
      i += 2;
      while (i < n) {
        if (text.at(i) == QChar(0x07)) {
          ++i;
          break;
        }
        if (text.at(i) == QChar(0x1b) && i + 1 < n &&
            text.at(i + 1) == QLatin1Char('\\')) {
          i += 2;
          break;
        }
        ++i;
      }
    } else if (k == QLatin1Char('P') || k == QLatin1Char('^') ||
               k == QLatin1Char('_') || k == QLatin1Char('X')) {
      // DCS/APC/PM/SOS：至 ST
      i += 2;
      while (i < n) {
        if (text.at(i) == QChar(0x1b) && i + 1 < n &&
            text.at(i + 1) == QLatin1Char('\\')) {
          i += 2;
          break;
        }
        ++i;
      }
    } else {
      // 单字符 ESC 序列（如 ESC = / ESC >）直接丢弃
      i += 2;
    }
  }
  return out;
}

GoalMarkers GoalProtocol::parse(const QString &text) {
  GoalMarkers m;
  // 只剥离转义序列。编码规范化（normalizeReceived）由调用方（GoalRunner）对
  // 原始 receivedData 预处理后再喂入——协议层不碰编码，避免损坏已是干净 UTF-8
  // 的文本。
  const QString clean = stripAnsi(text);

  static const QRegularExpression planRe(QStringLiteral("\\[GOAL-PLAN\\]"));
  m.hasPlan = planRe.match(clean).hasMatch();

  static const QRegularExpression doneRe(QStringLiteral("\\[GOAL-DONE\\]"));
  m.hasDone = doneRe.match(clean).hasMatch();

  // [GOAL-STEP n/N] 标题 [| 推荐方案: 摘要]
  static const QRegularExpression stepRe(
      QStringLiteral("\\[GOAL-STEP\\s+(\\d+)\\s*/"
                     "\\s*(\\d+)\\]\\s*([^|\\r\\n]*?)(?:\\s*\\|\\s*推荐方案[:"
                     "：]\\s*(.+))?\\s*$"),
      QRegularExpression::MultilineOption);
  const auto sm = stepRe.match(clean);
  if (sm.hasMatch()) {
    bool okC = false, okT = false;
    const int cur = sm.captured(1).toInt(&okC);
    const int tot = sm.captured(2).toInt(&okT);
    if (okC && okT && tot > 0 && cur >= 1 && cur <= tot) {
      m.hasStep = true;
      m.stepCurrent = cur;
      m.stepTotal = tot;
      m.stepTitle = sm.captured(3).trimmed();
      m.recommendation = sm.captured(4).trimmed();
    }
  }
  return m;
}

QString GoalProtocol::classifyModelError(const QString &text) {
  // 同 parse：只剥离转义序列，不做编码规范化（调用方预处理）。
  const QString clean = stripAnsi(text);
  // 模式表写死（FR-9）。ASCII 类用大小写不敏感，中文直接匹配。
  struct {
    const char *kind;
    const QRegularExpression re;
  } static const kPatterns[] = {
      {"timeout",
       QRegularExpression(
           QStringLiteral(
               "\\btimed?\\s+out\\b|\\binfo\\s+timeout\\b|"
               "\\btimeout\\b(?!\\s*[=:])|ETIMEDOUT|超时|请求?已?超时"),
           QRegularExpression::CaseInsensitiveOption)},
      {"rate_limit",
       QRegularExpression(
           QStringLiteral(
               "429|rate\\s*limit|too\\s+many\\s+requests|限流|速度限制"),
           QRegularExpression::CaseInsensitiveOption)},
      {"server",
       QRegularExpression(
           QStringLiteral("\\b5\\d{2}\\b\\s*(internal|server|error)?|"
                          "internal\\s+server\\s+error|服务端错误|服务器错误"),
           QRegularExpression::CaseInsensitiveOption)},
      {"network",
       QRegularExpression(
           QStringLiteral("ECONNRESET|ECONNREFUSED|EPIPE|ENOTFOUND|network\\s+"
                          "error|fetch\\s+failed|网络中断|网络错误"),
           QRegularExpression::CaseInsensitiveOption)},
      {"empty",
       QRegularExpression(
           QStringLiteral(
               "no\\s+response|empty\\s+response|无返回|没有返回|无响应"),
           QRegularExpression::CaseInsensitiveOption)},
      {"refusal",
       QRegularExpression(
           QStringLiteral("content\\s+(filter|policy)|refused|被拒绝|内容拒绝"),
           QRegularExpression::CaseInsensitiveOption)},
  };
  for (const auto &p : kPatterns) {
    if (p.re.match(clean).hasMatch())
      return QString::fromLatin1(p.kind);
  }
  return {};
}

QString GoalProtocol::contractPrompt(const QString &goal,
                                     const QString &acceptance) {
  QString p;
  p += QStringLiteral("【/goal 目标契约】\n");
  p += QStringLiteral("目标：%1\n").arg(goal);
  if (!acceptance.trimmed().isEmpty())
    p += QStringLiteral("最终验收命令（必须独立执行并通过）：%1\n")
             .arg(acceptance);
  p += QStringLiteral(
      "\n执行协议（严格遵守）：\n"
      "1. 先输出一行：[GOAL-PLAN] 然后列出编号步骤清单（每步一行 N. 描述）。\n"
      "2. 每开始一步输出：[GOAL-STEP n/N] 步骤描述 | 推荐方案: "
      "你检索并整理出的最优方案摘要。\n"
      "3. 遇到方案选型/技术决策，先用 websearch/webfetch "
      "检索，整理出最优解与理由，"
      "然后直接采用推荐方案继续执行，不要停下来询问我。\n"
      "4. 小步推进：每完成一步立即开始下一步，不要等待确认。\n"
      "5. 全部完成且验收命令通过后，最后一行输出：[GOAL-DONE]\n"
      "\n现在开始，先输出 [GOAL-PLAN]。\n");
  return p;
}

QString GoalProtocol::resumePrompt(const QString &goal, int doneSteps,
                                   int totalSteps,
                                   const QString &lastStepTitle) {
  QString p;
  p += QStringLiteral("【/goal 断点续跑】此前对话中断，目标不变。\n");
  p += QStringLiteral("目标：%1\n").arg(goal);
  p += QStringLiteral("进度：已完成 %1/%2 步").arg(doneSteps).arg(totalSteps);
  if (!lastStepTitle.trimmed().isEmpty())
    p += QStringLiteral("，最后完成：%1").arg(lastStepTitle);
  p += QStringLiteral(
      "\n请先重新输出 [GOAL-PLAN] 列出全部步骤，并把已完成步骤标注（完成），"
      "然后从中断处按协议继续执行（[GOAL-STEP n/N] … | 推荐方案: … → 完成后 "
      "[GOAL-DONE]）。\n");
  return p;
}

} // namespace codearts
