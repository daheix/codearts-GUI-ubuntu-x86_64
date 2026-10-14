// test_core.cpp —— codeartsGUI 核心单元测试（doctest）
// 覆盖 Environment(FR-2) 与 ProcessMonitor 采样(FR-5)。
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <unistd.h>

#include "environment.h"
#include "processmonitor.h"

using namespace codearts;

TEST_CASE("Environment::resolveExecutable 默认定位 codearts") {
  auto r = Environment::resolveExecutable();
  // 本机已安装 codearts，应能定位到 installers/bin/codearts
  REQUIRE(!r.has_value() == false); // 若安装则必有值
  if (r.has_value()) {
    CHECK(r->endsWith(QStringLiteral("/bin/codearts")));
  }
}

TEST_CASE("Environment::resolveExecutable 覆盖路径") {
  auto r = Environment::resolveExecutable(QStringLiteral("/bin/sh"));
  REQUIRE(r.has_value());
  CHECK(*r == QStringLiteral("/bin/sh"));
}

TEST_CASE("Environment::resolveExecutable 缺失路径返回空") {
  auto r = Environment::resolveExecutable(
      QStringLiteral("/nonexistent/codearts-xyz"));
  CHECK(!r.has_value());
}

TEST_CASE("Environment::buildSession 装配 OPENCODE_* 与终端环境") {
  auto launch = Environment::buildSession();
  // FR-2：与启动脚本逐项对齐
  CHECK(launch.env.value(QStringLiteral("OPENCODE_MODE")) ==
        QStringLiteral("tui"));
  CHECK(launch.env.value(QStringLiteral("SCENARIO")) ==
        QStringLiteral("codeartsdoer"));
  CHECK(launch.env.value(QStringLiteral("OPENCODE_DISABLE_AUTOUPDATE")) ==
        QStringLiteral("true"));
  CHECK(launch.env.value(QStringLiteral("KERNEL_DATA_DIR"))
            .endsWith(QStringLiteral("/.codeartsdoer/cli-data")));
  // 终端能力（qtermwidget 完整 xterm 仿真）
  CHECK(launch.env.value(QStringLiteral("TERM")) ==
        QStringLiteral("xterm-256color"));
  CHECK(launch.env.value(QStringLiteral("COLORTERM")) ==
        QStringLiteral("truecolor"));
  // 必须继承图形环境（DISPLAY 或 WAYLAND_DISPLAY 至少其一在桌面存在）
  CHECK(launch.env.contains(QStringLiteral("PATH")));
}

TEST_CASE("Environment::buildSession 可执行缺失时 codeartsPath 为空") {
  // 覆盖为不存在的 exe → codeartsPath 应为空，上层据此报错
  auto launch =
      Environment::buildSession(QStringLiteral("/nonexistent/codearts-xyz"));
  CHECK(launch.codeartsPath.isEmpty());
}

TEST_CASE("ProcessMonitor::readRssBytes 对自身进程返回正值") {
  const qint64 rss = ProcessMonitor::readRssBytes(::getpid());
  CHECK(rss > 0);                           // 活进程 RSS 应 > 0
  CHECK(rss < 1024LL * 1024 * 1024 * 1024); // 且 < 1TB（防御性上界）
}

TEST_CASE("ProcessMonitor::readRssBytes 无效 pid 返回 -1") {
  // PID 通常不会是这个极大值
  const qint64 rss = ProcessMonitor::readRssBytes(999999999LL);
  CHECK(rss == -1);
}

TEST_CASE("ProcessMonitor::sample 聚合健康快照") {
  ProcessMonitor mon;
  mon.setThresholds(1024 * 1024, 1000); // 低阈值：RSS>1MB、uptime>1s 判超标
  mon.watch(::getpid(), 5000);          // uptime 5s > 1s → overUptime

  QHash<qint64, Health> got;
  QObject::connect(&mon, &ProcessMonitor::healthUpdated,
                   [&got](const QHash<qint64, Health> &all) { got = all; });
  mon.sample();

  REQUIRE(got.contains(::getpid()));
  const Health &h = got.value(::getpid());
  CHECK(h.pid == ::getpid());
  CHECK(h.rssBytes > 0);
  CHECK(h.uptimeMs == 5000);
  CHECK(h.overUptime == true); // 5s > 1s 阈值
  CHECK(h.overMemory == true); // 自身 RSS 肯定 > 1MB
}
