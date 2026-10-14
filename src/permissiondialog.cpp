// permissiondialog.cpp —— 权限询问弹窗实现（T17）
#include "permissiondialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace codearts {

PermissionDialog::PermissionDialog(const QString &permission,
                                   const QString &operation, QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(QStringLiteral("权限询问"));
  resize(460, 220);

  auto *info = new QLabel(this);
  info->setWordWrap(true);
  info->setText(
      QStringLiteral("<b>%1</b> 权限请求：<br>%2")
          .arg(permission.toHtmlEscaped(), operation.toHtmlEscaped()));

  auto *note = new QLabel(QStringLiteral("驱动已暂停，等待你的处置。"), this);
  note->setStyleSheet(QStringLiteral("color:#b58a1a;"));

  auto *buttons = new QDialogButtonBox(this);
  auto *allow = buttons->addButton(QStringLiteral("允许一次"),
                                   QDialogButtonBox::AcceptRole);
  auto *always = buttons->addButton(QStringLiteral("本目标总是允许"),
                                    QDialogButtonBox::AcceptRole);
  auto *deny =
      buttons->addButton(QStringLiteral("拒绝"), QDialogButtonBox::RejectRole);
  Q_UNUSED(allow);
  Q_UNUSED(deny);

  connect(allow, &QPushButton::clicked, this, [this]() {
    m_decision = PermissionDecision::AllowOnce;
    accept();
  });
  connect(always, &QPushButton::clicked, this, [this]() {
    m_decision = PermissionDecision::AlwaysAllow;
    accept();
  });
  connect(deny, &QPushButton::clicked, this, [this]() {
    m_decision = PermissionDecision::Deny;
    accept();
  });

  auto *root = new QVBoxLayout(this);
  root->addWidget(info);
  root->addWidget(note);
  root->addWidget(buttons);
}

PermissionDecision PermissionDialog::decision() const { return m_decision; }

} // namespace codearts
