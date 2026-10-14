// permissiondialog.h —— /goal 期间权限询问弹窗（T17，FR-10/FR-11）
// 拦截到 TUI 权限询问时暂停驱动，弹此框；用户选择注入 PTY 应答。
// 铁律：应答超时=暂停+黄条，绝不自动批准（无静默 fallback）。
#pragma once

#include <QDialog>

class QLabel;

namespace codearts {

// 用户对一次权限询问的处置。
enum class PermissionDecision {
  AllowOnce, // 允许一次（本次注入 y/回车）
  Deny,      // 拒绝（注入 n）
  AlwaysAllow, // 本目标总是允许（回写 global.json 该 permission/pattern=allow）
};

class PermissionDialog : public QDialog {
  Q_OBJECT
public:
  // permission 类型（edit/write/webfetch/…）、operation 操作内容摘要。
  PermissionDialog(const QString &permission, const QString &operation,
                   QWidget *parent = nullptr);

  PermissionDecision decision() const;

private:
  PermissionDecision m_decision = PermissionDecision::Deny;
};

} // namespace codearts
