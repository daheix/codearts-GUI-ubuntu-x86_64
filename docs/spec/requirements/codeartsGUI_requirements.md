# codeartsGUI 需求规范（EARS 格式）

> 产品：CodeArts CLI/TUI 的 Qt6 桌面 GUI 包装器
> 核心痛点：官方 VS Code 插件长会话（>1h）后"1秒仅3字符"卡死
> Qt6：ChinaSimStdio vcpkg 静态 6.11.2；终端组件 qtermwidget 2.4.0（静态）

## 0. 根因结论（阶段0 实验）

| 实验 | 结论 |
|------|------|
| HTTP 路线 | ❌ 作废。`codearts serve`/`run` 均需华为云 `CODEARTS_CLI_AK/SK`，本地 OAuth(auth.json) 仅够 TUI 用 |
| PTY 路线 | ✅ 可行。TUI 在 PTY 正常启动渲染标准 xterm 转义序列，qtermwidget 可原生处理 |
| 卡顿根因 | 高度指向**渲染层**：VS Code 用 xterm.js(webview) 渲染流式文本是经典瓶颈。qtermwidget(原生 C++ 终端仿真器)效率高得多，天然规避。内核层退化无法排除，架构做进程隔离兜底 |

## 1. 功能性需求（EARS）

### FR-1 终端嵌入（PTY 桥接）
- **WHEN** 用户新建会话，系统 **SHALL** 以 qtermwidget 终端组件启动一个 `codearts` TUI 子进程（PTY），并在界面中实时渲染其输出。
- **WHEN** TUI 输出 True Color/光标/备用屏等 xterm 转义序列，系统 **SHALL** 正确仿真显示（不乱码、不错位）。
- **WHEN** 用户在终端区域键入，系统 **SHALL** 将按键直通 PTY 子进程。

### FR-2 环境自动装配
- 系统 **SHALL** 复用 `~/.codeartsdoer/installers/codearts` 启动脚本的 `OPENCODE_*` 环境变量，用户无需手动配置。
- **WHEN** codearts 可执行文件缺失或路径非法，系统 **SHALL** 给出明确中文错误提示。

### FR-3 多会话管理
- 系统 **SHALL** 支持同时开启多个会话，以标签页(Tab)形式切换。
- **WHEN** 用户新建/关闭/切换标签，系统 **SHALL** 立即响应且互不干扰。
- **WHEN** 某会话的 TUI 子进程退出，系统 **SHALL** 保留该标签并显示退出状态，允许用户重启。

### FR-4 防卡顿（核心）
- **WHEN** 会话运行时间增长或输出量增大，界面交互 **SHALL NOT** 出现可感知卡顿（主线程永不阻塞在 PTY IO 上）。
- 系统 **SHALL** 限制终端回滚缓冲行数上限（默认 5000 行），防止长会话内存无限膨胀。
- 系统 **SHALL** 将每个会话的 TUI 运行于独立子进程，界面进程与内核进程隔离。
- **WHEN** 某会话子进程疑似卡死（长时间无输出且无响应），系统 **SHALL** 提供"重启会话"操作，且不影响其他会话与界面本体。

### FR-5 会话健康监控
- 系统 **SHALL** 在状态栏显示各会话子进程 PID、运行时长、内存占用（RSS）。
- **WHEN** 子进程内存超过阈值（默认 2GB）或已运行超过阈值（默认 60min），系统 **SHALL** 给出可选的"建议重启"视觉提示（不强制打断）。

### FR-6 桌面集成
- 系统 **SHALL** 提供 `.desktop` 启动器，双击即可启动，无需终端。
- **WHEN** 从桌面启动，系统 **SHALL** 自动定位 codearts 并装配环境。

## 2. 非功能性需求（NFR）

- **NFR-1 性能**：主窗口常驻内存 < 300MB（不含各会话子进程）；连续输出 10万行时 UI 事件循环延迟 < 50ms。
- **NFR-2 稳定性**：单会话子进程崩溃/卡死 **SHALL NOT** 导致界面进程退出。
- **NFR-3 可维护**：C++ 代码遵循 TDD（doctest）；改动后 `clang-format -i`；文件 < 500 行、函数 < 50 行。
- **NFR-4 可移植**：构建复用 ChinaSimStdio vcpkg 静态 Qt6，qtermwidget 与 lxqt-build-tools 经 FetchContent 固定 tag 拉取。

## 3. 明确不做（Out of Scope）
- 不实现 HTTP server 模式（被 AK/SK 锁死）。
- 不重新实现终端转义序列解析（用 qtermwidget）。
- 不修改 codearts 内核（只做进程级包装）。

---

# 第二期需求（v2，2026-10-14 用户确认开工）

## FR-7 /goal 自愈目标驱动（核心）

- **WHEN** 用户在 goal 对话框输入一句话目标（可附验收命令），系统 **SHALL** 下发"目标契约"，由 AI 自行规划编号步骤并逐项执行，GUI 监督驱动直至 100% 验收。
- **契约协议**：AI 输出 `[GOAL-PLAN]` 步骤清单 → `[GOAL-STEP n/N] <步骤> | 推荐方案: <摘要>` 每步打标 → `[GOAL-DONE]` 完成申报。
- **WHEN** 收到 `[GOAL-DONE]`，系统 **SHALL** 独立执行用户给定验收命令核验；**仅当 AI 自报与判据双过**才算 100% 完成（判据不过 → 注入失败原因，继续驱动）。
- **断点续跑**：进度 SHALL 持久化到 `.goal_state.json`；**WHEN** GUI 重启或会话重建，一键/自动读取进度并从中断处继续。
- **中断自愈（写死，永久）**：WHEN TUI 进程死亡或疑似卡死，SHALL 立即重启会话并带进度上下文续跑，**无次数上限**。

## FR-8 推荐方案自动推进（避免每次询问）

- **WHEN** 驱动中遇到方案选型/技术决策，AI SHALL 先 websearch/webfetch 检索，由 AI 整理"最优解+推荐理由"后**直接采用推荐方案执行**，不得停下询问用户。
- 全局停点仅两个：权限询问（"每次询问"模式下）与最终验收。

## FR-9 模型错误永久重试（写死参数）

- **WHEN** 发生**任何模型错误**（无返回/超时/速度限制429/5xx/网络中断/空响应/内容拒绝），系统 SHALL 立即重试当前步骤指令。
- **参数写死**：第 1 次立即；连续失败后间隔 60s（封顶）；**次数无上限、永久重试、永不放弃**；每次重试记审计日志（第 N 次/错误类型/时间）。
- **WHEN** 连续失败 ≥10 次，SHALL 显示黄条警示（可见、**但继续重试不打断**）。
- 检测 SHALL 双通道：`receivedData` 错误模式匹配 + 落盘指纹（会话 idle 且无新 assistant 消息）。
- 停滞续航（AI 空闲无进度标记）与模型错误共用同一重试引擎，同样永久续航（废除"n 次后交还用户"）。

## FR-10 权限控制体系接入与管理

- 系统 SHALL 集中读写 codearts 权限三处落盘：`storage/permission/global.json`（规则表：permission/pattern/action）、`storage/permission/config.json`（bash_mode）、`~/.codeartsdoer/settings.json`（信任目录）。
- **WHEN** 用户在设置页修改规则/bash_mode/信任目录，SHALL 即时落盘，并明示"对之后启动的会话生效"（不做运行中会话热补丁）。
- **/goal 询问操作**：WHEN 驱动期间出现权限询问，SHALL 暂停驱动 → GUI 弹「权限询问」对话框（权限类型+操作内容+允许一次/拒绝/本目标总是允许）→ 用户选择注入 PTY；"本目标总是允许" SHALL 回写 global.json 持久化。
- **禁止静默放行**（无 fallback 铁律）：WHEN 应答超时，SHALL 暂停 goal + 黄条提示，**绝不自动批准**。

## FR-11 权限级别（二选一）

- goal 对话框 SHALL 提供权限级别：**每次询问**（默认，安全——所有询问走 FR-10 弹框）/ **完全控制**（显式勾选——goal 期间询问自动允许，UI 常驻明示"完全控制已开启"，全部操作落审计日志）。
- 完全控制为**用户显式选择的策略**，须满足"来源/触发/审计"三要素（非静默 fallback）。

## FR-12 双主题（浅色 + 深色，默认深色）

- 系统 SHALL 提供深色与浅色两套主题，**默认深色**；菜单即时切换，`QSettings` 持久化。
- 主题 SHALL 覆盖主窗口/标签/工具栏/状态栏/对话框，并 **qtermwidget 终端配色跟随**。

## NFR-5 开源发布

- 仓库标题 `codearts-GUI-ubuntu-x86_64`，**MIT 协议**（LICENSE 落盘）。
- Release SHALL 含：源码（tag）+ **完整安装包**（Ubuntu x86_64：tar.gz + .deb）+ README（构建/用法/架构/截图位）。
- 构建 SHALL 可被第三方复现：Qt6 来源显式可配（`-DCODEARTS_QT6_PREFIX`，默认 ChinaSimStdio vcpkg 静态 Qt6，缺省时用系统 Qt6——配置项明示，非隐藏 fallback）；依赖拉取（lxqt-build-tools/qtermwidget 固定 tag）在 README 记录。
- CI（GitHub Actions）：tag 触发 → 构建 + 测试 + 生成安装包 + 自动创建 Release。
