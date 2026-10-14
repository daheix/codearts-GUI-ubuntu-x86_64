// permissionstore.h —— codearts 权限规则库集中读写（FR-10）
// 对象：storage/permission/global.json（规则表）与 config.json（bash_mode）。
// 铁律：非法 JSON 显式失败，不猜、不兜底；回写保留无关字段。
#pragma once

#include <QList>
#include <QString>

namespace codearts {

struct PermissionRule {
  QString permission; // edit/write/webfetch/browser/websearch/task/skill…
  QString pattern;    // "*" 或路径 glob
  QString action;     // allow/ask/deny
};

class PermissionStore {
public:
  // dataDir = ~/.codeartsdoer/cli-data（即 KERNEL_DATA_DIR）。
  explicit PermissionStore(QString dataDir);

  QString rulesPath() const;    // <dataDir>/storage/permission/global.json
  QString bashModePath() const; // <dataDir>/storage/permission/config.json

  // 读规则表。文件缺失→空表（无规则即无授权，安全语义）；JSON 损坏→空表并置
  // lastError。
  QList<PermissionRule> rules();

  // 新增或覆盖同 (permission,pattern) 条目并回写；保留其他条目与字段。
  // 失败（损坏/不可写）返回 false 并置 lastError。
  bool setRule(const QString &permission, const QString &pattern,
               const QString &action);
  bool removeRule(const QString &permission, const QString &pattern);

  // bash_mode（config.json）。缺失/损坏→显式错误。
  QString bashMode();
  bool setBashMode(const QString &mode);

  QString lastError() const;

private:
  QString m_dataDir;
  QString m_lastError;
};

} // namespace codearts
