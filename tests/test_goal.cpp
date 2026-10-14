// test_goal.cpp —— /goal 驱动核心纯逻辑测试（T10 GoalProtocol / T11 GoalState /
// T12 RetryEngine） 覆盖 FR-7（契约协议/断点）、FR-9（永久重试写死参数）。
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "goalprotocol.h"
#include "goalstate.h"
#include "retryengine.h"

using namespace codearts;

// ─────────────────────────── T10 GoalProtocol ───────────────────────────

TEST_CASE("GoalProtocol::normalizeReceived 还原 UTF-8 中文") {
  // receivedData 是 fromLatin1 产物：中文 UTF-8 字节被逐字节映射为 Latin1 码点
  const QString mangled = QString::fromLatin1(QByteArray("收到收到"));
  const QString fixed = GoalProtocol::normalizeReceived(mangled);
  CHECK(fixed == QStringLiteral("收到收到"));
}

TEST_CASE("GoalProtocol::stripAnsi 剥离 CSI/OSC 序列") {
  const QString withEsc = QStringLiteral(
      "\x1b[38;2;255;0;0mGOAL-STEP\x1b[0m\x1b]0;CodeArts | hi\x07");
  const QString clean = GoalProtocol::stripAnsi(withEsc);
  CHECK(clean.contains(QStringLiteral("GOAL-STEP")));
  CHECK(!clean.contains(QChar(0x1b)));
  CHECK(!clean.contains(QChar(0x07)));
}

TEST_CASE("GoalProtocol::parse 解析 GOAL-PLAN/STEP/DONE 与推荐方案") {
  const QString plan = QStringLiteral("[GOAL-PLAN] 1.调研 2.实现 3.测试");
  CHECK(GoalProtocol::parse(plan).hasPlan);

  const QString step = QStringLiteral(
      "[GOAL-STEP 2/5] 实现解析器 | 推荐方案: 用 QRegularExpression 优先");
  const auto r = GoalProtocol::parse(step);
  CHECK(r.hasStep);
  CHECK(r.stepCurrent == 2);
  CHECK(r.stepTotal == 5);
  CHECK(r.stepTitle.contains(QStringLiteral("实现解析器")));
  CHECK(r.recommendation.contains(QStringLiteral("QRegularExpression")));

  const QString done = QStringLiteral("[GOAL-DONE] 全部完成，验收命令已通过");
  CHECK(GoalProtocol::parse(done).hasDone);
}

TEST_CASE("GoalProtocol::parse 非法串不崩且无标记") {
  const auto r = GoalProtocol::parse(
      QStringLiteral("普通对话，无任何标记。[GOAL-STEP x/y]"));
  CHECK(!r.hasPlan);
  CHECK(!r.hasDone);
  // n/y 非法索引 → 不认作有效步骤
  CHECK(!r.hasStep);
}

TEST_CASE("GoalProtocol::classifyModelError 覆盖六类模型错误") {
  CHECK(GoalProtocol::classifyModelError(QStringLiteral(
            "Request timeout after 30s")) == QStringLiteral("timeout"));
  CHECK(GoalProtocol::classifyModelError(QStringLiteral(
            "HTTP 429 Too Many Requests")) == QStringLiteral("rate_limit"));
  CHECK(GoalProtocol::classifyModelError(QStringLiteral(
            "HTTP 500 internal error")) == QStringLiteral("server"));
  CHECK(GoalProtocol::classifyModelError(
            QStringLiteral("ECONNRESET by peer")) == QStringLiteral("network"));
  CHECK(GoalProtocol::classifyModelError(QStringLiteral("模型无响应")) ==
        QStringLiteral("empty"));
  CHECK(GoalProtocol::classifyModelError(QStringLiteral(
            "content policy refusal")) == QStringLiteral("refusal"));
  // 正常回复不应误报
  CHECK(GoalProtocol::classifyModelError(
            QStringLiteral("[GOAL-STEP 1/2] 调研完成"))
            .isEmpty());
}

TEST_CASE("GoalProtocol::contractPrompt 含协议三标记与自主决策条款") {
  const QString p = GoalProtocol::contractPrompt(QStringLiteral("修复登录缺陷"),
                                                 QStringLiteral("pytest -q"));
  CHECK(p.contains(QStringLiteral("修复登录缺陷")));
  CHECK(p.contains(QStringLiteral("pytest -q")));
  CHECK(p.contains(QStringLiteral("[GOAL-PLAN]")));
  CHECK(p.contains(QStringLiteral("[GOAL-STEP")));
  CHECK(p.contains(QStringLiteral("[GOAL-DONE]")));
  CHECK(p.contains(QStringLiteral("websearch"))); // 自主权条款：检索后直行
}

TEST_CASE("GoalProtocol::resumePrompt 含已完成进度") {
  const QString p = GoalProtocol::resumePrompt(QStringLiteral("目标X"), 2, 5,
                                               QStringLiteral("设计"));
  CHECK(p.contains(QStringLiteral("目标X")));
  CHECK(p.contains(QStringLiteral("2/5")));
  CHECK(p.contains(QStringLiteral("设计")));
  CHECK(p.contains(QStringLiteral("继续")));
}

// ─────────────────────────── T11 GoalState ───────────────────────────

TEST_CASE("GoalState 序列化往返一致（断点续跑）") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral(".goal_state.json"));

  GoalState s;
  s.goal = QStringLiteral("目标Y");
  s.acceptance = QStringLiteral("make test");
  s.totalSteps = 3;
  s.status = QStringLiteral("driving");
  s.steps = {{1, QStringLiteral("调研"), true, QString()},
             {2, QStringLiteral("实现"), true, QStringLiteral("方案A")},
             {3, QStringLiteral("测试"), false, QString()}};
  REQUIRE(s.save(path));

  GoalState loaded;
  REQUIRE(loaded.load(path));
  CHECK(loaded.goal == s.goal);
  CHECK(loaded.acceptance == s.acceptance);
  CHECK(loaded.totalSteps == 3);
  CHECK(loaded.doneSteps() == 2);
  CHECK(loaded.status == QStringLiteral("driving"));
  REQUIRE(loaded.steps.size() == 3);
  CHECK(loaded.steps[1].recommendation == QStringLiteral("方案A"));
}

TEST_CASE("GoalState 损坏 JSON 显式失败不猜") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral(".goal_state.json"));
  {
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("{ this is not json");
  }
  GoalState s;
  QString err;
  CHECK(!s.load(path, &err)); // 显式失败
  CHECK(!err.isEmpty());
}

TEST_CASE("GoalState 文件不存在显式失败") {
  GoalState s;
  QString err;
  CHECK(!s.load(QStringLiteral("/nonexistent/dir/.goal_state.json"), &err));
  CHECK(!err.isEmpty());
}

// ─────────────────────────── T12 RetryEngine ───────────────────────────

TEST_CASE("RetryEngine 首次失败立即重试（写死参数）") {
  RetryEngine e;
  CHECK(!e.shouldRetry(0));
  e.notifyError(1000, QStringLiteral("timeout"));
  CHECK(e.consecutiveFailures() == 1);
  CHECK(e.shouldRetry(1000)); // 第 1 次立即
  CHECK(e.nextRetryAtMs() == 1000);
}

TEST_CASE("RetryEngine 连续失败间隔 60s 封顶") {
  RetryEngine e;
  e.notifyError(1000, QStringLiteral("timeout"));
  e.notifyError(2000, QStringLiteral("rate_limit"));
  CHECK(e.nextRetryAtMs() == 2000 + RetryEngine::kRetryIntervalMs);
  CHECK(!e.shouldRetry(2000 + RetryEngine::kRetryIntervalMs - 1));
  CHECK(e.shouldRetry(2000 + RetryEngine::kRetryIntervalMs));
  // 再次失败仍是 60s（封顶不增长）
  e.notifyError(5000, QStringLiteral("server"));
  CHECK(e.nextRetryAtMs() == 5000 + RetryEngine::kRetryIntervalMs);
}

TEST_CASE("RetryEngine 次数无上限永久重试") {
  RetryEngine e;
  qint64 now = 0;
  for (int i = 0; i < 500; ++i) { // 500 次连续失败——引擎永不放弃
    const bool immediate = e.notifyError(now, QStringLiteral("timeout"));
    if (i == 0) {
      CHECK(immediate); // 首次立即
    }
    // 推进到预定重试时刻，必然到期可重试（无次数上限、无放弃路径）
    now = e.nextRetryAtMs();
    CHECK(e.shouldRetry(now));
    now += 1; // 视为已重试，进入下一轮
  }
  CHECK(e.consecutiveFailures() == 500);
  CHECK(e.warningActive()); // ≥10 已警示
}

TEST_CASE("RetryEngine 成功后复位且审计留痕") {
  RetryEngine e;
  e.notifyError(1000, QStringLiteral("timeout"));
  e.notifyError(61000, QStringLiteral("network"));
  CHECK(e.auditTrail().size() == 2);
  CHECK(e.auditTrail()[0].contains(QStringLiteral("#1")));
  CHECK(e.auditTrail()[1].contains(QStringLiteral("#2")));
  CHECK(e.auditTrail()[1].contains(QStringLiteral("network")));

  e.notifySuccess();
  CHECK(e.consecutiveFailures() == 0);
  CHECK(!e.warningActive());
  CHECK(!e.shouldRetry(1LL << 40)); // 成功后不再重试
}
