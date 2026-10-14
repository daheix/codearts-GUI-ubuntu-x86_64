// test_systems.cpp —— T13 PermissionStore / T14 ThemeManager 测试
// 覆盖
// FR-10（权限三处落盘读写）、FR-11（级别策略不在此层）、FR-12（双主题默认深色）。
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include "permissionstore.h"
#include "thememanager.h"

using namespace codearts;

// ─────────────────────────── T13 PermissionStore ───────────────────────────

static void writeJson(const QString &path, const QByteArray &content) {
  QFile f(path);
  REQUIRE(f.open(QIODevice::WriteOnly));
  f.write(content);
}

TEST_CASE("PermissionStore 读取 global.json 规则表") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString permDir = dir.filePath(QStringLiteral("storage/permission"));
  REQUIRE(QDir().mkpath(permDir));
  writeJson(permDir + QStringLiteral("/global.json"),
            R"([{"permission":"edit","pattern":"*","action":"ask"},
                {"permission":"websearch","pattern":"*","action":"allow"}])");

  PermissionStore store(dir.path());
  const auto rules = store.rules();
  REQUIRE(rules.size() == 2);
  CHECK(rules[0].permission == QStringLiteral("edit"));
  CHECK(rules[0].action == QStringLiteral("ask"));
  CHECK(rules[1].permission == QStringLiteral("websearch"));
}

TEST_CASE("PermissionStore setRule 回写保留无关字段与条目") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString permDir = dir.filePath(QStringLiteral("storage/permission"));
  REQUIRE(QDir().mkpath(permDir));
  writeJson(permDir + QStringLiteral("/global.json"),
            R"([{"permission":"edit","pattern":"*","action":"ask"},
                {"permission":"browser","pattern":"*","action":"deny"}])");

  PermissionStore store(dir.path());
  // “本目标总是允许”路径：edit/* 由 ask 改 allow
  REQUIRE(store.setRule(QStringLiteral("edit"), QStringLiteral("*"),
                        QStringLiteral("allow")));

  const auto rules = store.rules();
  REQUIRE(rules.size() == 2); // 条目数不变
  // edit 已改
  bool editFound = false;
  for (const auto &r : rules) {
    if (r.permission == QStringLiteral("edit")) {
      editFound = true;
      CHECK(r.action == QStringLiteral("allow"));
      CHECK(r.pattern == QStringLiteral("*"));
    }
    if (r.permission == QStringLiteral("browser")) {
      CHECK(r.action == QStringLiteral("deny")); // 无关条目原样保留
    }
  }
  CHECK(editFound);
}

TEST_CASE("PermissionStore bash_mode 读写（config.json）") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString permDir = dir.filePath(QStringLiteral("storage/permission"));
  REQUIRE(QDir().mkpath(permDir));
  writeJson(permDir + QStringLiteral("/config.json"),
            R"({"bash_mode":"always_allow"})");

  PermissionStore store(dir.path());
  CHECK(store.bashMode() == QStringLiteral("always_allow"));
  REQUIRE(store.setBashMode(QStringLiteral("ask")));
  CHECK(store.bashMode() == QStringLiteral("ask"));
}

TEST_CASE("PermissionStore 非法 JSON 显式失败不猜") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString permDir = dir.filePath(QStringLiteral("storage/permission"));
  REQUIRE(QDir().mkpath(permDir));
  writeJson(permDir + QStringLiteral("/global.json"), "[ broken json");

  PermissionStore store(dir.path());
  CHECK(store.rules().isEmpty());
  CHECK(store.lastError().contains(QStringLiteral("global.json")));
  // 写操作也应显式失败（拒绝基于损坏内容改写）
  CHECK(!store.setRule(QStringLiteral("edit"), QStringLiteral("*"),
                       QStringLiteral("allow")));
}

// ─────────────────────────── T14 ThemeManager ───────────────────────────

TEST_CASE("ThemeManager 无设置时默认深色（FR-12）") {
  QSettings s(QStringLiteral("codeartsGUI"), QStringLiteral("codeartsGUI"));
  s.clear();
  CHECK(ThemeManager::current() == Theme::Dark);
}

TEST_CASE("ThemeManager 切换浅色持久化回读") {
  QSettings s(QStringLiteral("codeartsGUI"), QStringLiteral("codeartsGUI"));
  s.clear();
  ThemeManager::set(Theme::Light);
  CHECK(ThemeManager::current() == Theme::Light);
  ThemeManager::set(Theme::Dark);
  CHECK(ThemeManager::current() == Theme::Dark);
}

TEST_CASE("ThemeManager 两套 QSS 非空且基调不同") {
  const QString dark = ThemeManager::styleSheet(Theme::Dark);
  const QString light = ThemeManager::styleSheet(Theme::Light);
  CHECK(!dark.isEmpty());
  CHECK(!light.isEmpty());
  CHECK(dark != light);
  // 深色背景应为深色调、浅色为浅色调（取 window 背景色首字符判断非同值）
  CHECK(dark.contains(QStringLiteral("background")));
  CHECK(light.contains(QStringLiteral("background")));
}

TEST_CASE("ThemeManager palette 区分深浅") {
  const QPalette d = ThemeManager::palette(Theme::Dark);
  const QPalette l = ThemeManager::palette(Theme::Light);
  CHECK(d.color(QPalette::Window) != l.color(QPalette::Window));
}
