// permissionpage.cpp —— 权限管理页实现（T17）
#include "permissionpage.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace codearts {

static const QStringList kActions = {
    QStringLiteral("allow"), QStringLiteral("ask"), QStringLiteral("deny")};

PermissionPage::PermissionPage(const QString &dataDir, QWidget *parent)
    : QWidget(parent), m_store(dataDir) {
  auto *root = new QVBoxLayout(this);

  auto *hint = new QLabel(
      QStringLiteral(
          "规则落盘于 "
          "storage/permission/global.json，修改对<b>之后启动</b>的会话生效。"),
      this);
  hint->setWordWrap(true);
  root->addWidget(hint);

  m_table = new QTableWidget(this);
  m_table->setColumnCount(3);
  m_table->setHorizontalHeaderLabels(
      {QStringLiteral("权限"), QStringLiteral("模式"), QStringLiteral("动作")});
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->horizontalHeader()->setSectionResizeMode(0,
                                                    QHeaderView::Interactive);
  m_table->horizontalHeader()->setSectionResizeMode(1,
                                                    QHeaderView::Interactive);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setAlternatingRowColors(true);
  root->addWidget(m_table, 1);

  auto *row = new QHBoxLayout;
  auto *addBtn = new QPushButton(QStringLiteral("新增规则"), this);
  auto *delBtn = new QPushButton(QStringLiteral("删除选中"), this);
  auto *reloadBtn = new QPushButton(QStringLiteral("重新加载"), this);
  row->addWidget(addBtn);
  row->addWidget(delBtn);
  row->addWidget(reloadBtn);
  row->addStretch();

  auto *bashLabel = new QLabel(QStringLiteral("bash 模式:"), this);
  m_bashMode = new QComboBox(this);
  m_bashMode->addItems({QStringLiteral("always_allow"), QStringLiteral("ask"),
                        QStringLiteral("never")});
  row->addWidget(bashLabel);
  row->addWidget(m_bashMode);
  root->addLayout(row);

  connect(addBtn, &QPushButton::clicked, this, &PermissionPage::onAddRow);
  connect(delBtn, &QPushButton::clicked, this, [this]() {
    const auto rows = m_table->selectedRanges();
    for (const auto &r : rows) {
      for (int i = r.bottomRow(); i >= r.topRow(); --i) {
        m_table->removeRow(i);
      }
    }
    saveRulesFromTable();
  });
  connect(reloadBtn, &QPushButton::clicked, this, &PermissionPage::reload);
  connect(m_table, &QTableWidget::cellChanged, this, [this](int, int) {
    if (!m_loading)
      saveRulesFromTable();
  });
  connect(m_bashMode, &QComboBox::currentIndexChanged, this,
          &PermissionPage::onBashModeChanged);

  reload();
}

void PermissionPage::reload() {
  m_loading = true;
  const auto rules = m_store.rules();
  if (!m_store.lastError().isEmpty()) {
    emit errorRaised(m_store.lastError());
  }
  m_table->setRowCount(0);
  m_table->setRowCount(rules.size());
  for (int i = 0; i < rules.size(); ++i) {
    m_table->setItem(i, 0, new QTableWidgetItem(rules[i].permission));
    m_table->setItem(i, 1, new QTableWidgetItem(rules[i].pattern));
    auto *actionCell = new QComboBox;
    actionCell->addItems(kActions);
    const int idx = kActions.indexOf(rules[i].action);
    actionCell->setCurrentIndex(idx >= 0 ? idx : 1); // 未知动作 → ask（保守）
    m_table->setCellWidget(i, 2, actionCell);
    connect(actionCell, &QComboBox::currentIndexChanged, this, [this](int) {
      if (!m_loading)
        saveRulesFromTable();
    });
  }
  const QString bm = m_store.bashMode();
  const int bidx = m_bashMode->findText(bm);
  if (bidx >= 0)
    m_bashMode->setCurrentIndex(bidx);
  m_loading = false;
}

void PermissionPage::saveRulesFromTable() {
  // 逐行回写。失败（损坏/不可写）上抛错误，不静默。
  for (int i = 0; i < m_table->rowCount(); ++i) {
    const QString perm =
        m_table->item(i, 0) ? m_table->item(i, 0)->text().trimmed() : QString();
    const QString pattern = m_table->item(i, 1)
                                ? m_table->item(i, 1)->text().trimmed()
                                : QStringLiteral("*");
    QString action = QStringLiteral("ask");
    if (auto *cb = qobject_cast<QComboBox *>(m_table->cellWidget(i, 2))) {
      action = cb->currentText();
    }
    if (perm.isEmpty())
      continue;
    if (!m_store.setRule(perm, pattern, action)) {
      emit errorRaised(m_store.lastError());
      return;
    }
  }
}

void PermissionPage::onAddRow() {
  const int row = m_table->rowCount();
  m_table->insertRow(row);
  m_table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("edit")));
  m_table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("*")));
  auto *cb = new QComboBox;
  cb->addItems(kActions);
  cb->setCurrentIndex(kActions.indexOf(QStringLiteral("ask")));
  m_table->setCellWidget(row, 2, cb);
  connect(cb, &QComboBox::currentIndexChanged, this, [this](int) {
    if (!m_loading)
      saveRulesFromTable();
  });
  saveRulesFromTable();
}

void PermissionPage::onBashModeChanged(int idx) {
  if (m_loading)
    return;
  const QString mode = m_bashMode->itemText(idx);
  if (!m_store.setBashMode(mode)) {
    emit errorRaised(m_store.lastError());
  }
}

} // namespace codearts
