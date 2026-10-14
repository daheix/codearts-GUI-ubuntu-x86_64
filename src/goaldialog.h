// goaldialog.h —— /goal 目标定义对话框（T16，FR-7/FR-11）
// 输入：一句话目标 + 可选验收命令 + 权限级别二选一（每次询问/完全控制）。
#pragma once

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;
class QRadioButton;
class QLabel;

namespace codearts {

class GoalDialog : public QDialog {
  Q_OBJECT
public:
  explicit GoalDialog(QWidget *parent = nullptr);

  QString goal() const;       // 目标一句话
  QString acceptance() const; // 验收命令（可空）
  // 权限级别：true=完全控制（显式勾选，常驻明示+审计），false=每次询问（默认）。
  bool fullControl() const;

private:
  void onAccept();

  QLineEdit *m_goalEdit = nullptr;
  QPlainTextEdit *m_acceptEdit = nullptr;
  QRadioButton *m_askRadio = nullptr;     // 每次询问（默认选中）
  QRadioButton *m_controlRadio = nullptr; // 完全控制
  QLabel *m_warn = nullptr;               // 完全控制明示黄条
};

} // namespace codearts
