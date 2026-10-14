// permissionpage.h —— 权限管理页（T17，FR-10）
// 表格展示 global.json 规则，支持改 action/新增/删除；bash_mode 下拉。
// 改动即时落盘，明示"对之后启动的会话生效"（不做运行中热补丁）。
#pragma once

#include <QWidget>

#include "permissionstore.h"

class QTableWidget;
class QComboBox;

namespace codearts {

class PermissionPage : public QWidget {
  Q_OBJECT
public:
  // dataDir = ~/.codeartsdoer/cli-data（KERNEL_DATA_DIR）。
  explicit PermissionPage(const QString &dataDir, QWidget *parent = nullptr);

  void reload(); // 从落盘重新加载到表格

signals:
  void errorRaised(QString message); // 非法 JSON/写失败上抛（显式，不静默）

private:
  void saveRulesFromTable();
  void onAddRow();
  void onBashModeChanged(int idx);

  PermissionStore m_store;
  QTableWidget *m_table = nullptr;
  QComboBox *m_bashMode = nullptr;
  bool m_loading = false; // 抑制回写循环
};

} // namespace codearts
