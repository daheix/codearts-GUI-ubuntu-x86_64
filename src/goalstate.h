// goalstate.h —— /goal 断点续跑持久化（.goal_state.json）
// FR-7：进度落盘，GUI 重启/会话重建后可从中断处继续。
#pragma once

#include <QString>
#include <QVector>

namespace codearts {

struct GoalStep {
  int index = 0;
  QString title;
  bool done = false;
  QString recommendation;
};

// /goal 驱动状态。序列化为 JSON；损坏/缺失显式失败（不猜）。
struct GoalState {
  QString goal;                            // 目标一句话
  QString acceptance;                      // 验收命令（可空）
  QString status = QStringLiteral("idle"); // idle/driving/verifying/done
  int totalSteps = 0;
  QString lastStepTitle; // 最后完成步骤标题（恢复注入用）
  QVector<GoalStep> steps;

  int doneSteps() const;

  // 读写 JSON 文件。失败返回 false 并置 error（显式失败，禁止兜底）。
  bool load(const QString &path, QString *error = nullptr);
  bool save(const QString &path, QString *error = nullptr) const;
};

} // namespace codearts
