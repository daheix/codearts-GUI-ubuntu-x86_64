// test_goalrunner.cpp —— T15 GoalRunner 状态机测试（mock 终端 + 固定时钟）
// 覆盖
// FR-7（契约/步进/判据双过/断点）、FR-9（永久重试）、FR-8（自主推进不问）。
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <QSignalSpy>
#include <QTemporaryDir>

#include "goalrunner.h"

using namespace codearts;

namespace {
// mock 终端：记录注入内容
class MockSink : public ITerminalSink {
public:
  QStringList injections;
  void inject(const QString &text) override { injections << text; }
};
} // namespace

TEST_CASE("GoalRunner 启动即下发契约并落盘断点") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);

  r.start(QStringLiteral("实现登录页"), QStringLiteral("pytest -q"));

  CHECK(r.phase() == GoalPhase::AwaitingModel);
  REQUIRE(sink.injections.size() == 1);
  CHECK(sink.injections[0].contains(QStringLiteral("[GOAL-PLAN]")));
  CHECK(sink.injections[0].contains(QStringLiteral("实现登录页")));
  CHECK(sink.injections[0].contains(QStringLiteral("pytest -q")));
  // 断点已落盘
  CHECK(QFile::exists(dir.filePath(QStringLiteral(".goal_state.json"))));
}

TEST_CASE("GoalRunner 步进标记推进进度") {
  QTemporaryDir dir;
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标"), QString());

  QSignalSpy prog(&r, &GoalRunner::progress);
  // AI 输出 [GOAL-PLAN] 后进入 [GOAL-STEP 1/3]
  r.feed(QStringLiteral("[GOAL-PLAN] 1.调研 2.实现 3.测试"));
  r.feed(QStringLiteral("[GOAL-STEP 1/3] 调研 | 推荐方案: 检索最优解"));

  CHECK(r.phase() == GoalPhase::Driving);
  CHECK(r.state().totalSteps == 3);
  REQUIRE(!prog.isEmpty());
  CHECK(prog.last()[2].toString().contains(QStringLiteral("调研")));
}

TEST_CASE("GoalRunner 收到 GOAL-DONE 进入验收核验") {
  QTemporaryDir dir;
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标"), QStringLiteral("make test"));

  QSignalSpy verify(&r, &GoalRunner::verifyRequested);
  r.feed(QStringLiteral("[GOAL-DONE] 全部完成"));
  CHECK(r.phase() == GoalPhase::Verifying);
  REQUIRE(verify.size() == 1);
  CHECK(verify[0][0].toString() == QStringLiteral("make test"));
}

TEST_CASE("GoalRunner 判据通过→100% 完成（AI 自报+判据双过）") {
  QTemporaryDir dir;
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标"), QStringLiteral("make test"));
  r.feed(QStringLiteral("[GOAL-DONE] 完成"));

  QSignalSpy done(&r, &GoalRunner::completed);
  r.onVerifyFinished(true, QStringLiteral("全部通过"));
  CHECK(r.phase() == GoalPhase::Done);
  CHECK(done.size() == 1);
}

TEST_CASE("GoalRunner 判据不过→注入失败原因继续驱动（不放弃）") {
  QTemporaryDir dir;
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标"), QStringLiteral("make test"));
  const int before = sink.injections.size();
  r.feed(QStringLiteral("[GOAL-DONE] 完成"));

  r.onVerifyFinished(false, QStringLiteral("1 个断言失败"));
  CHECK(r.phase() == GoalPhase::AwaitingModel); // 回到驱动
  REQUIRE(sink.injections.size() > before);
  CHECK(sink.injections.last().contains(QStringLiteral("验收未通过")));
}

TEST_CASE("GoalRunner 模型错误永久重试（写死：首次立即+60s封顶+无上限）") {
  QTemporaryDir dir;
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标"), QString());
  const int afterStart = sink.injections.size();

  // 首个错误：立即重试
  r.feed(QStringLiteral("Request timeout after 30s"));
  CHECK(r.retry().consecutiveFailures() == 1);
  CHECK(r.warningActive() == false);

  // 连续 12 次错误 → 黄条警示激活，但仍在重试（无上限）
  qint64 t = 1000;
  for (int i = 0; i < 12; ++i) {
    t += RetryEngine::kRetryIntervalMs;
    r.setClock(t);
    r.feed(QStringLiteral("HTTP 429 Too Many Requests"));
  }
  CHECK(r.retry().consecutiveFailures() >= 12);
  CHECK(r.warningActive() == true);           // ≥10 黄条
  CHECK(sink.injections.size() > afterStart); // 持续注入重试
}

TEST_CASE("GoalRunner 会话死亡→Recovering 断点续跑") {
  QTemporaryDir dir;
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标"), QString());
  r.feed(QStringLiteral("[GOAL-STEP 2/4] 实现 | 推荐方案: A"));

  r.onSessionDied();
  CHECK(r.phase() == GoalPhase::Recovering);
}

TEST_CASE("GoalRunner 断点文件损坏时 start 重置为新目标") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral(".goal_state.json"));
  {
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("{ corrupt");
  }
  MockSink sink;
  GoalRunner r(&sink, path);
  r.setClock(1000);
  r.start(QStringLiteral("新目标"), QString()); // 不崩，正常下发契约
  CHECK(r.phase() == GoalPhase::AwaitingModel);
  CHECK(r.state().goal == QStringLiteral("新目标"));
}

// ───── 看门狗 + 错误识别修复（用户反馈：info timeout 后停住不续跑）─────

TEST_CASE("GoalRunner 看门狗：目标进行中 60s 无输出即强制续跑（非正常重启）") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标X"), QString());
  const int after = sink.injections.size();

  // 30s 无输出：未到 1 分钟窗口，不触发
  r.tick(1000 + 30 * 1000);
  CHECK(sink.injections.size() == after);

  // 61s 持续无任何终端输出（正是 info timeout 后停住的情形）→ 强制续跑
  r.tick(1000 + 61 * 1000);
  CHECK(sink.injections.size() > after);
  CHECK(r.phase() == GoalPhase::AwaitingModel); // 重注后回到等待模型
  CHECK(r.retry().consecutiveFailures() >= 1);
}

TEST_CASE("GoalRunner 'info timeout' 真实文案被识别并立即重试") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标"), QString());
  const int after = sink.injections.size();

  r.feed(QStringLiteral("info timeout")); // TUI 实际显示的样式
  CHECK(r.retry().lastErrorKind() == QStringLiteral("timeout"));
  CHECK(r.retry().consecutiveFailures() == 1);
  CHECK(sink.injections.size() > after); // 立即重注，不停住
}

TEST_CASE("GoalRunner Driving 阶段出错也重试（不再被阶段门控吞掉）") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标"), QString());
  r.feed(QStringLiteral("[GOAL-STEP 1/2] 调研 | 推荐方案: A"));
  CHECK(r.phase() == GoalPhase::Driving);

  const int after = sink.injections.size();
  r.feed(QStringLiteral("info timeout")); // Driving 下出错同样要续航
  CHECK(r.retry().consecutiveFailures() >= 1);
  CHECK(sink.injections.size() > after);
}

TEST_CASE("GoalRunner 含 timeout 字样的进度标记不误判为错误") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标"), QString());
  r.retry().consecutiveFailures(); // noop
  r.feed(QStringLiteral("[GOAL-STEP 1/2] 配置请求 timeout 参数 | 推荐方案: x"));
  CHECK(r.retry().consecutiveFailures() == 0); // 进度优先，不误判
  CHECK(r.phase() == GoalPhase::Driving);
}

TEST_CASE("GoalRunner 会话中断→restart 后看门狗兜底续跑") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标"), QString());
  r.feed(QStringLiteral("[GOAL-STEP 1/2] 实现 | 推荐方案: A"));

  r.onSessionDied(); // UI 已 restart 会话，进入 Recovering
  CHECK(r.phase() == GoalPhase::Recovering);
  const int after = sink.injections.size();

  r.tick(1000 + 91 * 1000); // 重建后 90s 兜底，强制断点续跑
  CHECK(sink.injections.size() > after);
}

// ───── 看门狗 v2：抗心跳 + 阶段无关（真正修复 reported bug）─────

TEST_CASE("GoalRunner 抗心跳：只有转轮/控制帧(无文字)持续 → 60s 仍判停滞续跑") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标"), QString());
  const int after = sink.injections.size();

  // 模拟内核每 ~30s connection-monitor 心跳 +
  // 转轮动画：纯控制序列，无字母/中文。
  // 这些会刷新"最后输出时间"，但不含真实内容——旧版静默看门狗被骗过永不触发。
  qint64 t = 1000;
  for (int i = 0; i < 40; ++i) {
    t += 1000;
    r.setClock(t);
    r.feed(QStringLiteral("\x1b[?2026h\x1b[0;3H\x1b[?25h\x1b[0m"));
  }
  r.tick(t + 1000); // ~42s：未到 60s 内容窗口
  CHECK(sink.injections.size() == after);

  r.tick(t + 21000); // ~62s 无真实内容 → 强制续跑（关键：不被心跳欺骗）
  CHECK(sink.injections.size() > after);
  CHECK(r.retry().consecutiveFailures() >= 1);
}

TEST_CASE("GoalRunner 阶段无关无进度：Driving 下持续有文字但无新 GOAL-STEP → "
          "90s 判卡重启") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标"), QString());
  r.feed(QStringLiteral("[GOAL-STEP 1/3] 调研 | 推荐方案: A"));
  CHECK(r.phase() == GoalPhase::Driving);
  const int after = sink.injections.size();

  // 之后有真实文字持续流出（假活：模型在反复刷新但不再产出新的步骤标记），
  // 持续 95s，内容看门狗不触发，但"无步进"必须触发。
  qint64 t = r.nowMs();
  for (int i = 0; i < 95; ++i) {
    t += 1000;
    r.setClock(t);
    r.feed(QStringLiteral("filler output line without markers 0123"));
  }
  r.tick(t + 100);                       // ~95s 无 [GOAL-STEP]
  CHECK(sink.injections.size() > after); // Driving 下也强制重启续跑
}

TEST_CASE("GoalRunner interrupt(忙)信号在→不打断合法长步骤") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标"), QString());
  r.feed(QStringLiteral("[GOAL-STEP 1/3] 长步骤运行中 | 推荐方案: A"));
  const int after = sink.injections.size();

  // 模拟一个超过 60s 的长步骤：持续输出"esc interrupt"(忙) + 真实内容，
  // 但暂无新 [GOAL-STEP]。此时 interrupt 在=模型仍在工作，看门狗绝不能重启。
  qint64 t = r.nowMs();
  QStringList statuses;
  QObject::connect(&r, &GoalRunner::statusText, &r,
                   [&statuses](const QString &x) { statuses << x; });
  for (int i = 0; i < 100; ++i) {
    t += 1000;
    r.setClock(t);
    r.feed(
        QStringLiteral("running ... esc interrupt ... step progress text 42"));
  }
  r.tick(t + 100); // ~100s，仍应放行长步骤
  if (sink.injections.size() != after)
    MESSAGE("UNEXPECTED restart, statuses=",
            statuses.join(" | ").toStdString());
  CHECK(sink.injections.size() == after);
  CHECK(r.phase() == GoalPhase::Driving);
}

TEST_CASE("GoalRunner interrupt 消失+无进展 60s → 判定停住并重启续跑") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标"), QString());
  r.feed(QStringLiteral("[GOAL-STEP 2/5] 实现 | 推荐方案: A"));
  const int after = sink.injections.size();

  // 先有一段"工作中"（interrupt 在），随后中断（info timeout）→ interrupt
  // 消失、无进展。
  qint64 t = r.nowMs();
  for (int i = 0; i < 10; ++i) {
    t += 1000;
    r.setClock(t);
    r.feed(QStringLiteral("busy esc interrupt step 2 output"));
  }
  // 之后只剩转轮/心跳：无 interrupt、无新步进
  for (int i = 0; i < 65; ++i) {
    t += 1000;
    r.setClock(t);
    r.feed(QStringLiteral("\x1b[3;6H\x1b[?25h"));
  }
  r.tick(t + 100); // 距最后 interrupt>20s 且无进展>60s
  CHECK(sink.injections.size() > after); // 重启续跑
}

TEST_CASE("GoalRunner 停续 = 发送'继续完成剩余任务'，非重启全文契约") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  MockSink sink;
  GoalRunner r(&sink, dir.filePath(QStringLiteral(".goal_state.json")));
  r.setClock(1000);
  r.start(QStringLiteral("目标Z"), QString());
  r.feed(QStringLiteral("[GOAL-STEP 1/2] 实现登录 | 推荐方案: A"));
  const int after = sink.injections.size();
  qint64 t = r.nowMs();
  for (int i = 0; i < 65; i++) {
    t += 1000;
    r.setClock(t);
    r.feed(QStringLiteral("\x1b[3;6H\x1b[?25h"));
  }
  r.tick(t + 100);
  REQUIRE(sink.injections.size() > after);
  const QString last = sink.injections.last();
  CHECK(last.contains(QStringLiteral("继续完成剩余任务"))); // 是"继续"
  CHECK_FALSE(last.contains(QStringLiteral("目标契约"))); // 不是重启全文契约
  CHECK_FALSE(last.contains(QStringLiteral("目标Z"))); // 不重述目标
}
