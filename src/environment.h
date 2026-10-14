// environment.h —— codearts 运行环境装配（FR-2）
// 复用 ~/.codeartsdoer/installers/codearts 启动脚本的 OPENCODE_*/CODEARTS_*
// 环境口径。
#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <optional>

namespace codearts {

// 装配好的会话启动信息
struct SessionLaunch {
  QString codeartsPath; // 可执行文件绝对路径（真实 bin，非脚本）
  QProcessEnvironment env; // 装配好的环境
};

class Environment {
public:
  // 定位 codearts 可执行文件：默认 <installers>/bin/codearts，可用 override
  // 覆盖。 返回 nullopt 表示文件不存在或不可执行。
  static std::optional<QString>
  resolveExecutable(const QString &overridePath = {});

  // 以 installers 根目录为基准装配会话环境（注入 OPENCODE_*/CODEARTS_*/TERM）。
  // installersDir 为空时用默认 ~/.codeartsdoer/installers。
  static SessionLaunch buildSession(const QString &overrideExe = {},
                                    const QString &installersDir = {});

  // 装配用的 installers 默认目录
  static QString defaultInstallersDir();
};

} // namespace codearts
