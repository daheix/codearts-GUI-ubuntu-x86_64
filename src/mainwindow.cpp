// mainwindow.cpp —— 主窗口实现
#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>

#include "environment.h"
#include "goaldialog.h"
#include "goalrunner.h"
#include "permissionpage.h"
#include "processmonitor.h"
#include "sessionmanager.h"
#include "sessionwidget.h"
#include "thememanager.h"

#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QVBoxLayout>

namespace codearts {

namespace {
// 目标会话注入器：把 GoalRunner 的注入转发到 SessionWidget 终端。
class SessionSink : public ITerminalSink {
public:
  explicit SessionSink(SessionWidget *s) : m_s(s) {}
  void inject(const QString &text) override {
    if (m_s)
      m_s->sendText(text);
  }

private:
  SessionWidget *m_s;
};
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("CodeArts 工作台 v%1")
                     .arg(QCoreApplication::applicationVersion()));
  resize(1100, 720);

  m_mgr = new SessionManager(this);
  m_monitor = new ProcessMonitor(this);

  // 应用持久化主题（默认深色，FR-12）
  ThemeManager::apply(*qApp, ThemeManager::current());

  buildUi();
  buildMenus();

  // 健康监控 → 状态栏（FR-5）
  connect(m_monitor, &ProcessMonitor::healthUpdated, this,
          &MainWindow::onHealthUpdated);

  // 首个会话
  if (m_mgr->sessions().isEmpty()) {
    onNewSession();
  }
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
  m_tabs = new QTabWidget(this);
  m_tabs->setTabsClosable(true);
  m_tabs->setMovable(true);
  m_tabs->setDocumentMode(true);
  setCentralWidget(m_tabs);

  connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int idx) {
    if (auto *s = qobject_cast<SessionWidget *>(m_tabs->widget(idx))) {
      m_monitor->unwatch(s->pid());
      m_mgr->closeSession(s);
      m_tabs->removeTab(idx);
    }
  });
  connect(m_tabs, &QTabWidget::currentChanged, this,
          &MainWindow::updateStatusBar);

  // 工具栏
  m_toolbar = addToolBar(QStringLiteral("主工具栏"));
  m_toolbar->setMovable(false);
  auto *newAct = m_toolbar->addAction(QStringLiteral("新建会话"), this,
                                      &MainWindow::onNewSession);
  newAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
  // /goal 目标入口（T16）：醒目放工具栏首位，避免只藏在菜单里难以发现
  auto *goalAct = m_toolbar->addAction(QStringLiteral("🎯 /goal 目标"), this,
                                       &MainWindow::onStartGoal);
  goalAct->setToolTip(QStringLiteral(
      "定义一个目标，让 AI 自主规划步进并驱动到 100% 验收（Ctrl+G）"));
  goalAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
  auto *restartAct = m_toolbar->addAction(QStringLiteral("重启会话"), this,
                                          &MainWindow::onRestartCurrentSession);
  restartAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
  m_toolbar->addSeparator();
  m_toolbar->addAction(QStringLiteral("关闭会话"), this,
                       &MainWindow::onCloseCurrentSession);

  // 状态栏
  m_statusLabel = new QLabel(this);
  m_statusLabel->setText(QStringLiteral("就绪"));
  statusBar()->addWidget(m_statusLabel, 1);

  // /goal 区（T16）：进度与阶段
  m_goalLabel = new QLabel(this);
  m_goalLabel->setText(QStringLiteral(""));
  statusBar()->addPermanentWidget(m_goalLabel, 0);

  // 重试黄条（T16，FR-9：≥10 连续失败警示，不打断）
  m_retryLabel = new QLabel(this);
  m_retryLabel->setStyleSheet(
      QStringLiteral("QLabel{background:#5a4a12;color:#ffe08a;padding:2px 8px;"
                     "border-radius:3px;border:1px solid #8a741f;}"));
  m_retryLabel->setVisible(false);
  statusBar()->addPermanentWidget(m_retryLabel, 0);
}

void MainWindow::buildMenus() {
  QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
  fileMenu->addAction(QStringLiteral("新建会话(&N)"), this,
                      &MainWindow::onNewSession,
                      QKeySequence(QStringLiteral("Ctrl+T")));
  fileMenu->addAction(QStringLiteral("关闭当前会话(&C)"), this,
                      &MainWindow::onCloseCurrentSession,
                      QKeySequence(QStringLiteral("Ctrl+W")));
  fileMenu->addSeparator();
  fileMenu->addAction(QStringLiteral("退出(&Q)"), this, &QWidget::close,
                      QKeySequence(QStringLiteral("Ctrl+Q")));

  QMenu *sessionMenu = menuBar()->addMenu(QStringLiteral("会话(&S)"));
  sessionMenu->addAction(QStringLiteral("重启当前会话(&R)"), this,
                         &MainWindow::onRestartCurrentSession,
                         QKeySequence(QStringLiteral("Ctrl+R")));
  sessionMenu->addSeparator();
  sessionMenu->addAction(QStringLiteral("定义 /goal 目标(&G)"), this,
                         &MainWindow::onStartGoal,
                         QKeySequence(QStringLiteral("Ctrl+G")));

  // 外观主题（T18，FR-12：默认深色）
  QMenu *appearanceMenu = menuBar()->addMenu(QStringLiteral("外观(&A)"));
  m_themeGroup = new QActionGroup(this);
  m_themeGroup->setExclusive(true);
  auto addTheme = [&](const QString &text, Theme t) {
    auto *act =
        appearanceMenu->addAction(text, this, &MainWindow::onApplyTheme);
    act->setCheckable(true);
    act->setData(static_cast<int>(t));
    m_themeGroup->addAction(act);
    if (ThemeManager::current() == t)
      act->setChecked(true);
    return act;
  };
  addTheme(QStringLiteral("深色（默认）"), Theme::Dark);
  addTheme(QStringLiteral("浅色"), Theme::Light);

  // 权限管理（T17，FR-10）
  QMenu *toolsMenu = menuBar()->addMenu(QStringLiteral("工具(&T)"));
  toolsMenu->addAction(QStringLiteral("权限管理(&P)"), this,
                       &MainWindow::onOpenPermissions);

  QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
  helpMenu->addAction(QStringLiteral("关于(&A)"), this, [this]() {
    QMessageBox::about(
        this, QStringLiteral("关于 CodeArts 工作台"),
        QStringLiteral("CodeArts 工作台 v%1\n"
                       "基于原生终端仿真的 CodeArts CLI/TUI 图形包装。\n"
                       "进程隔离 + 回滚上限 + 健康监控，解决长会话卡顿；\n"
                       "/goal 自愈驱动 + 双主题 + 权限管理 + 中文输入。\n"
                       "MIT License · ubuntu x86_64")
            .arg(QCoreApplication::applicationVersion()));
  });
}

void MainWindow::onNewSession() {
  // FR-2：环境缺失时明确报错
  if (Environment::resolveExecutable().value_or(QString()).isEmpty()) {
    showEnvError();
    return;
  }
  SessionWidget *s = m_mgr->createSession(m_tabs);
  if (!s) {
    showEnvError();
    return;
  }
  const int idx = m_tabs->addTab(s, s->title());
  m_tabs->setCurrentIndex(idx);

  // 会话退出 → 状态栏提示（保留标签供重启）
  connect(s, &SessionWidget::processExited, this, [this, s](SessionWidget *) {
    m_statusCache.insert(s->pid(), QStringLiteral("已退出"));
    updateStatusBar();
  });
  updateStatusBar();
}

void MainWindow::onCloseCurrentSession() {
  if (auto *s = currentSession()) {
    const int idx = m_tabs->indexOf(s);
    m_monitor->unwatch(s->pid());
    m_mgr->closeSession(s);
    if (idx >= 0) {
      m_tabs->removeTab(idx);
    }
    updateStatusBar();
  }
}

void MainWindow::onRestartCurrentSession() {
  if (auto *s = currentSession()) {
    const qint64 oldPid = s->pid();
    m_monitor->unwatch(oldPid);
    m_mgr->restartSession(s); // FR-4 L4
    updateStatusBar();
  }
}

void MainWindow::onHealthUpdated(const QHash<qint64, Health> &all) {
  // 同步监控集合（新增的 pid 纳入监控）
  for (SessionWidget *s : m_mgr->sessions()) {
    if (s->isRunning() && !all.contains(s->pid())) {
      m_monitor->watch(s->pid(), s->uptimeMs());
    }
  }
  m_statusCache.clear();
  for (auto it = all.begin(); it != all.end(); ++it) {
    const Health &h = it.value();
    const double mb = h.rssBytes / (1024.0 * 1024.0);
    const qint64 sec = h.uptimeMs / 1000;
    QString txt = QStringLiteral("PID %1 · %2MB · %3s")
                      .arg(h.pid)
                      .arg(mb, 0, 'f', 0)
                      .arg(sec);
    if (h.overMemory || h.overUptime) {
      txt += QStringLiteral(" ⚠建议重启");
    }
    m_statusCache.insert(h.pid, txt);
  }
  updateStatusBar();
}

void MainWindow::updateStatusBar() {
  if (auto *s = currentSession()) {
    QString txt = m_statusCache.value(s->pid());
    if (txt.isEmpty()) {
      txt = s->isRunning() ? QStringLiteral("运行中 · PID %1").arg(s->pid())
                           : QStringLiteral("已退出");
    }
    m_statusLabel->setText(QStringLiteral("[%1/%2] %3")
                               .arg(m_tabs->currentIndex() + 1)
                               .arg(m_tabs->count())
                               .arg(txt));
  } else {
    m_statusLabel->setText(QStringLiteral("无会话"));
  }
}

void MainWindow::onStartGoal() {
  auto *s = currentSession();
  if (!s || !s->isRunning()) {
    QMessageBox::information(this, QStringLiteral("/goal"),
                             QStringLiteral("请先启动一个运行中的会话。"));
    return;
  }
  GoalDialog dlg(this);
  if (dlg.exec() != QDialog::Accepted)
    return;

  // 接入驱动器到当前会话
  wireGoal(s);
  if (!m_goal)
    return;
  m_goal->start(dlg.goal(), dlg.acceptance());
  // 权限级别：完全控制=显式策略（UI 明示+审计）；每次询问=默认拦截。
  m_goalLabel->setText(dlg.fullControl() ? QStringLiteral("/goal · 完全控制")
                                         : QStringLiteral("/goal · 每次询问"));
}

void MainWindow::onApplyTheme() {
  auto *act = qobject_cast<QAction *>(sender());
  if (!act)
    return;
  const Theme t = static_cast<Theme>(act->data().toInt());
  ThemeManager::set(t);
  ThemeManager::apply(*qApp, t);
}

void MainWindow::onOpenPermissions() {
  // 权限管理页（FR-10）：dataDir 取 Environment 会话口径的 KERNEL_DATA_DIR。
  const auto launch = Environment::buildSession();
  const QString dataDir = launch.env.value(QStringLiteral("KERNEL_DATA_DIR"));

  auto *page = new PermissionPage(dataDir);
  auto *dlg = new QDialog(this);
  dlg->setWindowTitle(QStringLiteral("权限管理"));
  dlg->resize(640, 480);
  auto *scroll = new QScrollArea(dlg);
  scroll->setWidget(page);
  scroll->setWidgetResizable(true);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
  connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
  auto *root = new QVBoxLayout(dlg);
  root->addWidget(scroll, 1);
  root->addWidget(buttons);
  connect(page, &PermissionPage::errorRaised, dlg, [dlg](const QString &msg) {
    QMessageBox::warning(dlg, QStringLiteral("权限落盘错误"), msg);
  });
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->show();
}

void MainWindow::wireGoal(SessionWidget *s) {
  if (m_goal) {
    m_goal->deleteLater();
    m_goal = nullptr;
  }
  if (m_goalSink) {
    delete m_goalSink;
    m_goalSink = nullptr;
  }
  m_goalSink = new SessionSink(s);
  // 断点文件落在会话工作目录 .goal_state.json
  const QString statePath =
      QDir::current().filePath(QStringLiteral(".goal_state.json"));
  m_goal = new GoalRunner(m_goalSink, statePath, this);

  // 终端输出 → 驱动器
  connect(s, &SessionWidget::outputReceived, m_goal, &GoalRunner::feed);

  // 在会话标签上打目标徽标：让"哪个标签在跑目标 + 进度"一眼可见（界面可见性）
  auto badgeTab = [this, s](const QString &txt) {
    const int idx = m_tabs->indexOf(s);
    if (idx >= 0)
      m_tabs->setTabText(idx, txt);
  };

  // 阶段 → 状态栏
  connect(m_goal, &GoalRunner::statusText, this,
          [this](const QString &t) { m_goalLabel->setText(t); });
  connect(
      m_goal, &GoalRunner::progress, this,
      [this, badgeTab](int done, int total, const QString &title) {
        m_goalLabel->setText(
            QStringLiteral("/goal %1/%2 · %3").arg(done).arg(total).arg(title));
        badgeTab(QStringLiteral("\u{1F3AF} %1/%2 · %3")
                     .arg(done)
                     .arg(total)
                     .arg(title.left(16)));
      });
  // 重试黄条（FR-9：≥10 警示，不打断）
  connect(m_goal, &GoalRunner::retrying, this,
          [this](int consecutive, const QString &kind) {
            if (consecutive >= RetryEngine::kWarnThreshold) {
              m_retryLabel->setText(
                  QStringLiteral("⚠ 模型错误连续 %1 次(%2)，永久重试中")
                      .arg(consecutive)
                      .arg(kind));
              m_retryLabel->setVisible(true);
            }
          });
  connect(m_goal, &GoalRunner::completed, this,
          [this, s, badgeTab](const QString &summary) {
            m_retryLabel->setVisible(false);
            m_goalLabel->setText(QStringLiteral("/goal 完成"));
            badgeTab(QStringLiteral("\u2705 ") + s->title());
            QMessageBox::information(this, QStringLiteral("/goal 完成"),
                                     summary);
          });
  // 驱动节拍（FR-9 + 看门狗）：每 1s 调一次 tick；tick 内部以 60s 窗口判停滞、
  // 到期重试泵、恢复兜底——目标进行中一旦对话非正常（无输出/无进度）即强制续跑。
  auto *timer = new QTimer(m_goal);
  timer->setInterval(1000);
  connect(timer, &QTimer::timeout, this, [this]() {
    if (m_goal)
      m_goal->tick(QDateTime::currentMSecsSinceEpoch());
  });
  timer->start();
  // 会话退出 → 驱动器进入 Recovering（永久续航）
  connect(s, &SessionWidget::processExited, this, [this, s](SessionWidget *) {
    m_retryLabel->setVisible(false);
    if (m_goal && m_goal->phase() != GoalPhase::Idle &&
        m_goal->phase() != GoalPhase::Done) {
      s->restart(); // 重建终端，保证有活的注入目标（L4 自愈）
      m_goal->onSessionDied(); // 进入 Recovering，tick 看门狗负责续跑
    }
  });
}

SessionWidget *MainWindow::currentSession() const {
  return qobject_cast<SessionWidget *>(m_tabs->currentWidget());
}

void MainWindow::showEnvError() {
  QMessageBox::warning(
      this, QStringLiteral("无法启动会话"),
      QStringLiteral("未找到 codearts 可执行文件：\n%1\n\n"
                     "请确认已安装 CodeArts "
                     "CLI（~/.codeartsdoer/installers/bin/codearts）。")
          .arg(Environment::resolveExecutable().value_or(
              Environment::defaultInstallersDir() + "/bin/codearts")));
}

void MainWindow::closeEvent(QCloseEvent *event) {
  // 各 SessionWidget 随标签页父子关系在窗口销毁时析构，其析构调 teardown()
  // 杀掉 PTY 子进程（FR-4 L1 进程隔离下的主动清理）。此处仅记录退出。
  for (SessionWidget *s : m_mgr->sessions()) {
    m_monitor->unwatch(s->pid());
  }
  QMainWindow::closeEvent(event);
}

} // namespace codearts
