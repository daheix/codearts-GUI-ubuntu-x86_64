# CodeArts GUI（ubuntu-x86_64）

> 面向华为云码道 CodeArts AI 智能体 CLI/TUI 的原生 Qt6 图形工作台。
> 进程隔离防卡顿 + `/goal` 自愈目标驱动 + 双主题 + 权限管理 + 模型错误永久重试。

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Ubuntu%20x86--64-lightgrey)
![Qt](https://img.shields.io/badge/Qt-6.11-green)

---

## 为什么做这个

官方 CodeArts 插件跑在 VS Code 的 xterm.js webview 里，长会话（>1h）会出现
**"1 秒仅 3 字符"的渲染卡死**。本项目绕开 webview，直接用 **qtermwidget 原生终端仿真**
接管 CodeArts TUI 的 PTY，从根上消除渲染层瓶颈，并叠加四层防护：

| 层 | 机制 | 解决 |
|----|------|------|
| L1 | 每会话独立 PTY 子进程 | 会话间互不拖累，单会话崩溃不传染 |
| L2 | Qt 主线程零阻塞（异步 IO） | 界面永不冻结 |
| L3 | 回滚缓冲上限 `setHistorySize(5000)` | 长会话内存不无限膨胀 |
| L4 | 健康监控（RSS/uptime）+ 一键重启 | 退化可发现、可恢复 |

## 核心能力

### `/goal` 自愈目标驱动

输入**一句话目标**（可附验收命令），AI 自行规划编号步骤并逐项执行，GUI 监督驱动直至 100% 验收完成：

- **契约协议**：AI 输出 `[GOAL-PLAN]` 步骤清单 → `[GOAL-STEP n/N] … | 推荐方案: …` 逐步打标 → `[GOAL-DONE]` 完成申报。
- **判据双过**：AI 自报 `[GOAL-DONE]` 后，程序**独立执行验收命令**核验；仅当自报 + 判据双过才算 100%（不过则注入失败原因继续驱动）。
- **推荐方案自动推进**：遇技术决策，AI 先 `websearch`/`webfetch` 检索，整理最优解后**直接执行不停问**。
- **断点续跑**：进度持久化到 `.goal_state.json`；会话中断/重启后一键从断点继续。
- **中断自愈**：进程死亡 → 立即重启会话并带进度上下文续跑，**无次数上限**。

### 模型错误永久重试（写死参数）

任何模型错误（无返回 / 超时 / 429 限流 / 5xx / 网络中断 / 空响应 / 内容拒绝）立即重试：

| 参数 | 值 |
|------|----|
| 首次重试 | 立即 |
| 连续失败间隔 | 60s（封顶） |
| 重试次数 | **无上限，永久，永不放弃** |
| 警示 | 连续 ≥10 次 → 状态栏黄条（继续重试不打断） |
| 审计 | 每次记 `#N kind=… time=…` |

### 双主题（默认深色）

浅色 / 深色两套 QSS + QPalette，默认深色，菜单即时切换并持久化。

### 权限管理

集中读写 CodeArts 权限规则库（`storage/permission/global.json` 的 allow/ask/deny、`config.json` 的 `bash_mode`）。
`/goal` 期间的权限询问可拦截为弹窗（允许一次 / 拒绝 / 本目标总是允许），或按"完全控制"级别自动采用推荐方案（显式勾选 + 常驻明示 + 全程审计）。

## 如何启动 /goal（目标驱动）——3 步

> /goal 把"让 AI 自己把一个目标做完"变成一键可见的能力，不是手动敲命令。等价入口：工具栏「🎯 /goal 目标」、菜单 `会话 → 定义 /goal 目标`、快捷键 **Ctrl+G**。

1. **启动 GUI 后**，先点工具栏 **「新建会话」** 起一个运行中的 CodeArts TUI（/goal 需要一个活动会话来驱动）。
2. 点工具栏最显眼的 **「🎯 /goal 目标」**（或 Ctrl+G）。
3. 弹窗里填：
   - **目标**：一句话，例如"为登录模块补齐单元测试并全部跑绿"
   - **验收命令**（可空）：例如 `pytest -q tests/test_login.py` —— 完成后由程序**独立**执行核验
   - **权限级别**：`每次询问`（默认，安全）或 `完全控制`（AI 遇方案选型自动采用推荐方案不停问，全程审计）
   - 点 **「启动 /goal」**。

之后 AI 先输出 `[GOAL-PLAN]` 步骤清单，再按 `[GOAL-STEP n/N] … | 推荐方案: …` 逐步推进；窗口底部**状态栏 /goal 区**实时显示进度（如 `/goal 1/3 · 调研`），模型出错时**状态栏黄条**提示连续重试次数（永不放弃），会话崩溃自动拉起续跑。**AI 自报 `[GOAL-DONE]` 且验收命令独立执行通过，才算 100% 完成。**

## 架构

```
┌─ Qt6 主窗口 (MainWindow)
│    ├─ 会话标签 (SessionWidget × N)  ← 每个 = 独立 QTermWidget + PTY 子进程
│    ├─ /goal 驱动器 (GoalRunner)      ← 契约注入 / 步进解析 / 永久重试 / 断点续跑
│    │      ├─ GoalProtocol  协议解析（GOAL-PLAN/STEP/DONE、错误模式匹配）
│    │      ├─ GoalState     断点持久化 (.goal_state.json)
│    │      └─ RetryEngine   永久重试状态机（写死参数）
│    ├─ 主题 (ThemeManager)  双主题默认深色
│    └─ 权限 (PermissionStore / PermissionPage / PermissionDialog)
└─ 底层：qtermwidget 2.4.0 (静态) ← 原生 xterm 仿真，接管 CodeArts TUI 的 PTY
```

注入路径：`GoalRunner → ITerminalSink → SessionWidget::sendText → QTermWidget → PTY`。
输出捕获：`PTY → QTermWidget::receivedData → GoalRunner::feed → GoalProtocol 解析`。

## 构建

依赖：CMake ≥3.22、C++20 编译器、Qt6（Core/Gui/Widgets/Network/Test）。

### Qt6 来源（二选一，CMake 显式打印所选来源）

1. **ChinaSimStdio vcpkg 静态 Qt6**（默认探测）：`/home/wsc/wsc/ChinaSimStdio/vcpkg/installed/x64-linux`
2. **系统 Qt6**：未探测到 vcpkg 前缀时走 `find_package(Qt6)`。

可用 `-DCODEARTS_QT6_PREFIX=<path>` 显式指定。

### 命令

```bash
git clone https://github.com/daheix/codearts-GUI-ubuntu-x86_64.git
cd codearts-GUI-ubuntu-x86_64

./build.sh              # 配置 + 构建
./build.sh test         # 跑全部单元测试（4 套件 / 38 用例 / 653 断言）
./build.sh run          # 启动 GUI
```

首次构建会经 FetchContent 拉取并静态编译依赖（固定 tag）：
`lxqt-build-tools 2.4.0` → `qtermwidget 2.4.0`（含 Qt6 化补丁）。

### 打包

```bash
cmake --build build --target package   # 产出 .tar.gz + .deb（Ubuntu x86_64）
```

## 运行

```bash
QT_QPA_PLATFORM=xcb ./build/codeartsGUI
```

> 静态 Qt6 仅含 xcb 平台插件，须设 `QT_QPA_PLATFORM=xcb`。

前置：已安装 CodeArts CLI/TUI（本机路径 `~/.codeartsdoer/installers/bin/codearts`）。

## 安装包内容

| 文件 | 说明 |
|------|------|
| `codeartsGUI-<ver>-ubuntu-x86_64.tar.gz` | 免安装 tar.gz（二进制 + .desktop + 安装脚本） |
| `codeartsGUI-<ver>-amd64.deb` | Debian/Ubuntu 包 |

## 测试

```bash
./build.sh test
```

| 套件 | 覆盖 |
|------|------|
| `codeartsGUI_tests` | Environment 环境装配 / ProcessMonitor 健康采样 |
| `goal_tests` | GoalProtocol 协议解析 / GoalState 断点 / RetryEngine 永久重试 |
| `goalrunner_tests` | GoalRunner 状态机（契约/步进/判据双过/永久重试/断点续跑） |
| `systems_tests` | PermissionStore 权限读写 / ThemeManager 双主题 |

## 免责声明

本项目是对 CodeArts CLI/TUI 的**第三方图形包装**，非华为官方产品。CodeArts 是华为云商标。
本工具不修改 CodeArts 内核，仅做进程级包装与终端仿真。

## License

[MIT](LICENSE)
