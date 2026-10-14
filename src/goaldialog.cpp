// goaldialog.cpp —— /goal 目标定义对话框实现（T16）
#include "goaldialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace codearts {

GoalDialog::GoalDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(QStringLiteral("定义 /goal 目标"));
  resize(520, 360);

  auto *form = new QFormLayout;
  m_goalEdit = new QLineEdit(this);
  m_goalEdit->setPlaceholderText(
      QStringLiteral("一句话目标，例如：为登录页补齐单元测试并跑通"));
  form->addRow(QStringLiteral("目标"), m_goalEdit);

  m_acceptEdit = new QPlainTextEdit(this);
  m_acceptEdit->setPlaceholderText(QStringLiteral(
      "最终验收命令（可留空），例如：pytest -q tests/test_login.py\n"
      "完成后由程序独立执行核验，AI 自报 + 判据双过才算 100%。"));
  m_acceptEdit->setFixedHeight(72);
  form->addRow(QStringLiteral("验收命令"), m_acceptEdit);

  // 权限级别二选一（FR-11）
  m_askRadio = new QRadioButton(QStringLiteral("每次询问（默认，安全）"), this);
  m_askRadio->setChecked(true);
  m_controlRadio = new QRadioButton(
      QStringLiteral("完全控制（自动采用推荐方案，不停问）"), this);
  auto *permBox = new QVBoxLayout;
  permBox->addWidget(m_askRadio);
  permBox->addWidget(m_controlRadio);
  m_warn = new QLabel(this);
  m_warn->setWordWrap(true);
  m_warn->setStyleSheet(QStringLiteral(
      "QLabel{background:#5a4a12;color:#ffe08a;padding:6px;border-radius:4px;}"
      "QLabel{border:1px solid #8a741f;}"));
  m_warn->setText(
      QStringLiteral("⚠ 完全控制：AI 在 /goal "
                     "期间自动采用检索到的最优方案直接执行，不逐次询问。"
                     "所有操作将写入审计日志。仅在你信任该目标范围时选择。"));
  m_warn->setVisible(false);
  permBox->addWidget(m_warn);
  form->addRow(QStringLiteral("权限级别"), permBox);

  connect(m_controlRadio, &QRadioButton::toggled, this,
          [this](bool on) { m_warn->setVisible(on); });

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("启动 /goal"));
  buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
  connect(buttons, &QDialogButtonBox::accepted, this, &GoalDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *root = new QVBoxLayout(this);
  root->addLayout(form);
  root->addWidget(buttons);
}

QString GoalDialog::goal() const { return m_goalEdit->text().trimmed(); }
QString GoalDialog::acceptance() const {
  return m_acceptEdit->toPlainText().trimmed();
}
bool GoalDialog::fullControl() const { return m_controlRadio->isChecked(); }

void GoalDialog::onAccept() {
  if (goal().isEmpty()) {
    m_goalEdit->setFocus();
    return; // 目标不能为空，不关闭
  }
  accept();
}

} // namespace codearts
