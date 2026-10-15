// e2e_harness.cpp —— 真·端到端自愈验证（一次性排障工具，交付无关）
// 真实 QTermWidget + 真实 PTY 跑假 TUI（忙时渲染 esc interrupt，ESC→停，收"继续"→恢复），
// 接 GoalRunner；用假时钟快进 60s 触发看门狗，验证"停止→自动发继续→对话被拉起来"。
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <qtermwidget.h>

#include "goalrunner.h"

using namespace codearts;

static void processFor(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

class HarnessSink : public ITerminalSink {
public:
  QTermWidget *term = nullptr;
  QStringList injections;
  void inject(const QString &text) override {
    injections << text;
    if (term)
      term->sendText(text + QStringLiteral("\r"));
  }
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QTermWidget *term = new QTermWidget(0);
  term->resize(120, 40);
  term->show();

  term->setEnvironment({QStringLiteral("TERM=xterm-256color"),
                        QStringLiteral("COLORTERM=truecolor")});
  term->setShellProgram(QStringLiteral("/usr/bin/python3"));
  term->setArgs({QStringLiteral("tools/fake_tui.py"),
                 QStringLiteral("startbusy")});
  term->startShellProgram();

  HarnessSink sink;
  sink.term = term;
  const QString statePath = QDir::current().filePath(".goal_state.json");
  GoalRunner runner(&sink, statePath);

  QString captured;
  QObject::connect(term, &QTermWidget::receivedData, &runner,
                   [&runner, &captured](const QString &d) {
                     captured += QString::fromUtf8(d.toLatin1());
                     runner.feed(d);
                   });

  // 1) 下发目标契约；假 TUI 进入忙并渲染 [GOAL-STEP 1/3] + esc interrupt
  const qint64 base = 1000000000LL;
  runner.setClock(base);
  runner.start(QStringLiteral("端到端自愈验证"), QString());
  processFor(2600); // 等 TUI 启动 + 出步进/中断字样

  // 2) 用户连续 ESC 中断 → 假 TUI 停止（interrupt 消失）
  term->sendText(QStringLiteral("\x1b\x1b"));
  processFor(400);

  const int injectionsBefore = sink.injections.size();

  // 3) 假时钟快进 61s → 触发看门狗（无 interrupt 且无新进展）
  runner.setClock(base + 61000);
  runner.tick(base + 61000);
  processFor(1500); // 等"继续"送达 PTY + 假 TUI 恢复忙

  // ---- 结果 ----
  // 诊断
  QFile dbg(QDir::current().filePath(".tmp_e2e_dbg.txt"));
  dbg.open(QIODevice::WriteOnly | QIODevice::Text);
  { QTextStream d(&dbg);
    d << "phase=" << (int)runner.phase() << "\n";
    d << "totalSteps=" << runner.state().totalSteps
      << " doneSteps=" << runner.state().doneSteps() << "\n";
    d << "consecFail=" << runner.retry().consecutiveFailures() << "\n";
    d << "captured_len=" << captured.size() << "\n";
    d << "has_step=" << captured.contains(QStringLiteral("GOAL-STEP"))
      << " has_interrupt=" << captured.contains(QStringLiteral("interrupt")) << "\n";
    d << "captured_head=" << captured.left(200).replace(QChar(0x1b),QStringLiteral("<E>")) << "\n";
  }
  QFile out(QDir::current().filePath(".tmp_e2e_result.txt"));
  out.open(QIODevice::WriteOnly | QIODevice::Text);
  QTextStream ts(&out);
  const bool resumed = captured.contains(QStringLiteral("esc interrupt"));
  ts << "injections_before=" << injectionsBefore << "\n";
  ts << "injections_total=" << sink.injections.size() << "\n";
  ts << "last_injection_has_continue="
     << (!sink.injections.isEmpty() &&
         sink.injections.last().contains(QStringLiteral("继续")))
     << "\n";
  ts << "resumed_after_continue=" << resumed << "\n";
  for (const QString &s : sink.injections)
    ts << "INJ:" << s.left(40) << "\n";
  ts << "VERDICT="
     << ((!sink.injections.isEmpty() &&
          sink.injections.last().contains(QStringLiteral("继续")) && resumed)
             ? "PASS"
             : "FAIL")
     << "\n";
  out.close();
  fprintf(stderr, "[e2e] done, total_inj=%d\n", (int)sink.injections.size());
  return 0;
}
