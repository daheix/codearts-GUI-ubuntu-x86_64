// environment.cpp —— codearts 运行环境装配实现
#include "environment.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace codearts {

QString Environment::defaultInstallersDir() {
  return QDir::homePath() + QStringLiteral("/.codeartsdoer/installers");
}

std::optional<QString>
Environment::resolveExecutable(const QString &overridePath) {
  const QString path =
      overridePath.isEmpty()
          ? defaultInstallersDir() + QStringLiteral("/bin/codearts")
          : overridePath;

  const QFileInfo fi(path);
  if (!fi.exists() || !fi.isFile()) {
    return std::nullopt;
  }
  // 允许无可执行位（某些分发），但至少要是常规文件
  return path;
}

SessionLaunch Environment::buildSession(const QString &overrideExe,
                                        const QString &installersDir) {
  SessionLaunch launch;
  const QString exe = resolveExecutable(overrideExe).value_or(QString());
  launch.codeartsPath = exe;

  // 基于当前进程环境（继承 DISPLAY/WAYLAND/XAUTHORITY 等图形与用户态变量）
  launch.env = QProcessEnvironment::systemEnvironment();

  const QString home = QDir::homePath();
  const QString base =
      installersDir.isEmpty() ? defaultInstallersDir() : installersDir;

  // 与 ~/.codeartsdoer/installers/codearts 启动脚本逐项对齐
  launch.env.insert(QStringLiteral("package_tag"), QStringLiteral("undefined"));
  launch.env.insert(QStringLiteral("CODEAGENT_USE_MESSAGE_FEEDBACK"),
                    QStringLiteral("false"));
  launch.env.insert(QStringLiteral("OMO_SEND_ANONYMOUS_TELEMETRY"),
                    QStringLiteral("0"));
  launch.env.insert(QStringLiteral("SCENARIO"), QStringLiteral("codeartsdoer"));
  launch.env.insert(QStringLiteral("KERNEL_DATA_DIR"),
                    home + QStringLiteral("/.codeartsdoer/cli-data"));
  launch.env.insert(QStringLiteral("KERNEL_CONFIG_DIR"),
                    home + QStringLiteral("/.codeartsdoer"));
  launch.env.insert(QStringLiteral("OPENCODE_CHANNEL"),
                    QStringLiteral("latest"));
  launch.env.insert(QStringLiteral("OPENCODE_CONFIG"),
                    home + QStringLiteral("/.codeartsdoer/codearts_cli.json"));
  launch.env.insert(QStringLiteral("OPENCODE_CONFIG_FILE"),
                    QStringLiteral("codearts_cli.json,codearts_cli.jsonc"));
  launch.env.insert(QStringLiteral("OPENCODE_MODE"), QStringLiteral("tui"));
  launch.env.insert(QStringLiteral("PLUGIN_ENV"), QStringLiteral("hc"));
  launch.env.insert(QStringLiteral("NODE_TLS_REJECT_UNAUTHORIZED"),
                    QStringLiteral("0"));
  launch.env.insert(QStringLiteral("OPENCODE_DISABLE_MODELS_FETCH"),
                    QStringLiteral("1"));
  launch.env.insert(QStringLiteral("OPENCODE_DISABLE_AUTOUPDATE"),
                    QStringLiteral("true"));
  launch.env.insert(QStringLiteral("OPENCODE_ALWAYS_NOTIFY_UPDATE"),
                    QStringLiteral("false"));
  launch.env.insert(QStringLiteral("KERNEL_ACP"),
                    QString()); // 普通 TUI 模式为空

  // 终端能力：qtermwidget 是完整 xterm 仿真器
  launch.env.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
  launch.env.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));

  Q_UNUSED(base);
  return launch;
}

} // namespace codearts
