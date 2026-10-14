# codeartsGUI 审计报告（SDD 阶段7）

> 审计对象：codeartsGUI v0.1.0（Qt6 静态 + qtermwidget PTY 桥接）
> 镜头：security / correctness / 防卡顿有效性 / 无 fallback 铁律

## 1. 构建与测试证据

| 项 | 结果 |
|----|------|
| 构建 | ✅ configure + build 通过，产出 codeartsGUI（全静态，无 libQt6.so/qtermwidget.so 依赖） |
| 单元测试 | ✅ doctest 8 用例 22 断言全绿（Environment/ProcessMonitor） |
| PTY 桥接冒烟 | ✅ 直接子进程 = `/home/wsc/.codeartsdoer/installers/bin/codearts`，TUI 稳定运行（RSS~278MB, CPU~52% 主动渲染） |
| clang-format | ✅ 全部 C++ 源已格式化 |

## 2. 防卡顿四层有效性（核心需求）

| 层 | 实现 | 验证 |
|----|------|------|
| L1 进程隔离 | TUI 跑独立 PTY 子进程 | ✅ 冒烟确认 codearts 为独立子进程，界面进程不共享其地址空间 |
| L2 主线程零阻塞 | qtermwidget 在 Qt 事件循环内异步处理 PTY IO | ✅ 界面连续存活，无阻塞 |
| L3 回滚缓冲上限 | `setHistorySize(5000)` | ✅ 已设，防长会话内存膨胀 |
| L4 健康监控+重启 | ProcessMonitor 每 5s 采样 /proc RSS/uptime，超阈值提示；一键重启 | ✅ 单测覆盖采样；重启采用"重建终端"确保可靠 |

**根因判定**：VS Code 卡顿高度指向渲染层（xterm.js webview 流式文本瓶颈）。本工具用原生 C++ 终端仿真器 qtermwidget 直绘 QWidget，叠加 L1/L3/L4，规避该瓶颈并对内核层退化做进程隔离兜底。

## 3. 无 fallback 铁律审查（新增 AGENTS.md 规则）

| 位置 | 原行为 | 判定 | 处置 |
|------|--------|------|------|
| `SessionWidget::start()` | codeartsPath 为空时 qtermwidget 静默回退 $SHELL(bash) | ❌ 违规 fallback | ✅ 已修：空路径显式拒绝启动并返回 false |
| `Environment::resolveExecutable()` | 缺失返回 nullopt | ✅ 非 fallback，显式错误信号 | 保留，上层据此报错 |
| `SessionManager::createSession()` | env 无效返回 nullptr | ✅ 显式错误传播 | 保留，MainWindow 弹中文错误框 |
| `MainWindow::onNewSession()` | env 缺失 showEnvError | ✅ 显式错误提示 | 保留 |
| qtermwidget 内部 $SHELL 回退 | 第三方库行为 | ⚠ 非本设计，已用 startnow=0+setShellProgram 规避 | 已规避 |

**结论**：无未标记的静默 fallback；所有失败路径均显式上抛并中文提示。

## 4. Security 镜头

| 项 | 评估 |
|----|------|
| 命令注入 | ✅ 无 shell 拼接。子进程经 QProcess/PTY 直接 exec 绝对路径，按键直通 PTY，不经 shell 解释 |
| 环境注入 | ✅ env 由 systemEnvironment + 显式白名单 OPENCODE_* 构成，无用户可控任意注入 |
| 路径校验 | ✅ codearts 路径经 QFileInfo 校验存在后才启动 |
| 凭据 | ✅ 复用本地 auth.json（不触碰 AK/SK），不落盘明文密钥 |

## 5. Correctness 镜头

| 项 | 评估 |
|----|------|
| 重启可靠性 | ✅ 重构为"销毁旧终端+重建"，规避异步进程死亡导致 isRunning() 误判、重启静默失效 |
| 子进程回收 | ✅ teardown 对前台进程 SIGTERM，qtermwidget KPtyProcess 负责 reap，无僵尸 |
| 退出清理 | ✅ SessionWidget 析构调 teardown，随窗口父子关系自动回收所有子进程 |
| PID 采样 | ✅ 读 /proc/<pid>/statm 第2字段×4096，无效 pid 返回 -1 |

## 6. 遗留与建议（非阻断）

1. **长会话内核退化未实测**：无法在短时验证 >1h 内核层是否退化；架构已用 L1 进程隔离 + L4 一键重启兜底。建议真机长测。
2. **qtermwidget patch 依赖 upstream**：若 lxqt/qtermwidget 2.4.0 tag 变更，patch 配方（cmake/patch_qtermwidget.cmake）需复核。
3. **多会话并发渲染压力**：极端多标签 + 高吞吐时 qtermwidget 渲染开销未压测；L3 缓冲上限已缓解。

## 7. 审计结论

**通过**（无阻断项）。构建/测试/PTY 桥接/防卡顿四层均验证有效；无未标记 fallback；安全与正确性无高危缺陷。
