# codeartsGUI 技术设计

> 上游：`docs/spec/requirements/codeartsGUI_requirements.md`
> 技术栈：C++20 / Qt6 6.11.2(静态, vcpkg) / qtermwidget 2.4.0(静态) / CMake 3.22+

## 1. 总体架构

```
┌─────────────────────────── codeartsGUI (界面进程) ───────────────────────────┐
│                                                                              │
│  MainWindow (QMainWindow)                                                    │
│   ├─ QTabWidget (会话标签)                                                   │
│   │    └─ 每个 Tab = SessionWidget                                          │
│   │         ├─ QTermWidget (终端仿真器, 渲染 TUI 输出, 处理转义序列)          │
│   │         └─ (回滚缓冲由 QTermWidget 内部限制)                              │
│   ├─ StatusBar (各会话 PID/RSS/uptime/健康提示)                              │
│   └─ ToolBar/Menu (新建/关闭/重启/设置/关于)                                  │
│                                                                              │
│  SessionManager (会话生命周期)                                                │
│   └─ 维护 sessionId → {SessionWidget, QProcess/PID} 映射                      │
│                                                                              │
│  ProcessMonitor (健康监控, QTimer 周期采样 /proc/<pid>/statm)                 │
└──────────────────────────────────────────────────────────────────────────────┘
            │  PTY (每个会话一个独立伪终端)
            ▼
   ┌── codearts TUI 子进程 (进程1) ──┐   ┌── codearts TUI 子进程 (进程2) ──┐
   │  独立 OPENCODE_* 环境            │   │  ...                            │
   └──────────────────────────────────┘   └─────────────────────────────────┘
```

**关键决策**：界面进程与各 TUI 子进程**进程隔离**。子进程卡死/崩溃仅影响其自身标签，界面本体（事件循环）永不阻塞——这是防卡顿的兜底。

## 2. 防卡顿四层设计（对应 NFR-1/2, FR-4）

| 层 | 机制 | 实现 |
|----|------|------|
| L1 进程隔离 | TUI 跑独立子进程，界面不共享其地址空间/阻塞 | `QTermWidget::startShellProgram` fork PTY 子进程 |
| L2 主线程零阻塞 | PTY 读写由 qtermwidget 内部在 Qt 事件循环异步处理，绝不同步 read | qtermwidget 原生信号槽 |
| L3 回滚缓冲上限 | 限制终端历史行数，防长会话内存膨胀 | `QTermWidget::setHistoryLimit(5000)` |
| L4 健康监控+重启 | 周期采样 RSS/uptime，超阈值提示；一键重启会话 | `ProcessMonitor` + `/proc` 采样 |

**为什么这能解决 VS Code 卡顿**：VS Code 用 xterm.js 在 webview 里渲染流式文本，高吞吐下 DOM 操作成为瓶颈（渲染层）。qtermwidget 是原生 C++ 终端仿真器，直接绘制到 QWidget，同等转义序列处理开销低一个数量级；再叠加 L3 缓冲上限 + L1 进程隔离，长会话也不会拖垮界面。

## 3. 关键类设计

### SessionWidget（单会话视图）
```cpp
class SessionWidget : public QWidget {
    QTermWidget *m_term;      // 终端组件
    qint64       m_pid;       // TUI 子进程 PID
    QElapsedTimer m_uptime;   // 运行时长
public:
    explicit SessionWidget(const QString &codeartsPath,
                           const QStringList &env,   // 装配好的 OPENCODE_* 环境
                           QWidget *parent = nullptr);
    void start();             // 启动 TUI
    void restart();           // 杀旧启新（L4）
    qint64 pid() const; bool isRunning() const;
signals:
    void processExited(SessionWidget*);   // 子进程退出
};
```
- 构造时 `setHistoryLimit(5000)`、`setShellProgram(codeartsPath)`；`start()` 调 `startShellProgram()`。
- 连接 `sessionFinished` → 发 `processExited`。

### SessionManager（生命周期）
```cpp
class SessionManager : public QObject {
    QHash<qint64, SessionWidget*> m_byPid;
public:
    SessionWidget* createSession(QWidget* parentTabHost);
    void closeSession(qint64 pid); void restartSession(qint64 pid);
};
```

### ProcessMonitor（健康监控 L4）
- `QTimer` 每 5s 遍历各 `SessionWidget::pid()`，读 `/proc/<pid>/statm` 得 RSS（页×4KB），算 uptime。
- 阈值：RSS>2GB 或 uptime>60min → 对该标签置"建议重启"标记（状态栏黄色提示，不打断）。

### Environment::codeartsEnv()（FR-2）
- 读取 `~/.codeartsdoer/installers/codearts` 的环境变量口径，或直接 `QProcessEnvironment` 注入 `OPENCODE_*` 关键项 + `TERM=xterm-256color`。
- 校验可执行存在，缺失返回 `std::optional` 让上层报错。

## 4. 数据流
```
用户点"新建" → SessionManager::createSession
  → 构造 SessionWidget(codeartsPath, env)   // 注入 OPENCODE_* + TERM
  → start() → qtermwidget fork PTY → exec codearts TUI
  → TUI 输出(转义序列) → qtermwidget 内部解析 → 绘制 QWidget（L2 异步）
  → 用户按键 → qtermwidget → PTY → TUI
  → ProcessMonitor 每5s采样 RSS/uptime → 状态栏更新 → 超阈值置提示
  → "重启" → restart() → 杀子进程 → start() 新 PTY（其他会话不受影响）
```

## 5. 构建设计（CMake）
- 复用主项目静态 Qt6：`CMAKE_PREFIX_PATH` 前置 `vcpkg/installed/x64-linux`。
- 依赖链 FetchContent：
  1. `lxqt/lxqt-build-tools` tag `2.4.0` → `FetchContent` + `add_subdirectory`（提供 LXQtCompilerSettings 等宏）。
  2. `lxqt/qtermwidget` tag `2.4.0` → `FetchContent` + `PATCH_COMMAND`：
     - 移除 `find_package(Qt6LinguistTools REQUIRED)` 与 `include(LXQtTranslateTs)`（vcpkg 无 LinguistTools）
     - `lxqt_translate_ts` 调用加 `if(Qt6LinguistTools_FOUND)` 守卫
     - `add_library(... SHARED)` → `STATIC`
- 主目标链接：`Qt6::Core/Gui/Widgets/Network` + `qtermwidget6` + 静态传递闭包（fontconfig/freetype/expat...，expat 用 `--whole-archive`，详见根 CMakeLists 注释）。

## 6. 测试策略（TDD, doctest）
- `Environment`：给定 HOME，能算出正确 env（单测，不启动真 TUI）。
- `SessionManager`：create/close/restart 维护映射正确（mock 轻量 shell 如 `/bin/sh` 代替 codearts）。
- `ProcessMonitor`：对自身 pid 采样 RSS>0、uptime 递增。
- 集成：冒烟启动主窗口（offscreen/xcb），验证 Tab 创建。
- 说明：真实 codearts TUI 依赖登录态，单测用 `/bin/cat` 等替代 shell 验证 PTY 通路，避免测试依赖外部认证。

## 7. 对标映射
| 本工具 | 商业对标 | 差异 |
|--------|----------|------|
| codeartsGUI 会话管理 | VS Code CodeArts 插件 / Motor Workbench 会话树 | 原生终端 + 进程隔离，规避 webview 渲染瓶颈 |
| 防卡顿四层 | 无直接对标（痛点驱动） | 回滚上限 + 健康监控 + 一键重启 |

---

# 第二期设计（v2）—— /goal 自愈驱动 + 双主题 + 权限 + 开源

## 8. 模块总览（新增）

```
src/
├─ thememanager.{h,cpp}      # B/D 主题：dark/light QSS+QPalette，默认深色，QSettings 持久化
├─ permissionstore.{h,cpp}   # E 权限规则库读写：global.json / bash_mode / trustedFolders
├─ goalstate.{h,cpp}         # A 断点续跑：.goal_state.json 读写（目标/步骤/已完成/判据/状态）
├─ goalrunner.{h,cpp}        # A/F/G/H 核心状态机：契约下发/标记解析/自动推进/重试引擎/中断恢复
├─ goalprotocol.{h,cpp}      # G/H 纯逻辑（可单测）：GOAL-PLAN/STEP/DONE 解析、错误模式匹配
├─ permissiondialog.*        # F/E 权限询问弹窗（允许一次/拒绝/本目标总是允许）
├─ goaldialog.*              # C goal 输入框：目标 + 验收命令 + 权限级别二选一
└─ 权限管理页（设置对话框内）  # E 规则表格 + bash_mode + 信任目录
```

## 9. GoalRunner 状态机（A）

> **T9 spike 实测结论（2026-10-14）**
> - `sendText()` → 单个大按键事件 → PTY 字节；文本进 composer、`\r` 提交。daemon 日志确认用户消息落盘 + LLM 调用发出（GLM-5.2 tools=14）。
> - `receivedData(QString)` 为 `Session::receivedData` 的 `QString::fromLatin1` 产物 → **GoalRunner 解析前必须 `text.toLatin1()` 还原字节再 `QString::fromUtf8` 解码**（`GoalProtocol::normalizeReceived`），否则中文标记不可匹配。
> - 标题栏 `ESC]0;CodeArts | <composer内容>` 是 composer 实时镜像，可作**注入回显验证通道**；AI 生成的会话标题经 receivedData 到达 → 回复捕获链路成立。
> - **就绪时序风险**：TUI 启动后 composer 就绪前（实测 ~40s 内）注入的文本会被暂存、`\r` 不提交；~90s 注入才正常提交。→ 驱动须"等待就绪 + 验证式注入（未提交则重试）"。
> - 模型网关可能长时间无响应（实测 56s+），恰证 H 永久重试必要性。

```
Idle → ContractSent(下发契约) → Driving(步进) → Verifying(判据核验) → Done
          ↑        ↓                    ↓
   Recovering(重启+续跑) ← 进程死亡    RetryEngine(错误/停滞续航) ← 模型错误/停滞
```

- **驱动力**：`QTermWidget::sendText()`（spike 已验证注入路径）+ 回车提交。
- **判活三信号**：receivedData 输出流 / `opencode.db`+`session_diff` 落盘 mtime / 子进程存活。
- **契约 prompt 要点**（首条注入）：目标全文 + 步骤编号协议（`[GOAL-PLAN]`/`[GOAL-STEP n/N] | 推荐方案: …`/`[GOAL-DONE]`）+ **决策自主权条款**（检索→自荐→直行，禁止停问）+ 恢复上下文模板。
- **断点恢复**：重启后注入 `"目标X，已完成 n/N：…，请从中断处继续"`（进度来自 `.goal_state.json`，GUI 崩溃不丢）。

## 10. RetryEngine（H，写死参数）

| 参数 | 值（写死） |
|------|-----------|
| 触发 | 任何模型错误（无返回/超时/429限流/5xx/网络中断/空响应/内容拒绝）+ 会话停滞（idle 无进度标记） |
| 首次重试 | 立即 |
| 后续间隔 | 60s（封顶） |
| 次数 | **无上限，永久** |
| 警示 | 连续 ≥10 次 → 黄条（继续重试不打断） |
| 动作 | 重新 sendText 当前步骤指令 |
| 日志 | 每次记 `retry #N type=… time=…`（审计） |

- 错误检测双通道：`receivedData` 错误正则（超时|限流|429|5xx|ECONNRESET|请求失败|无返回…）+ 落盘指纹（idle 且 assistant 消息数无增长）。
- 进程死亡走 Recovering（重启同样永久重试）。**任何单一故障都无法使 /goal 停下**。

## 11. 权限体系（E/F）

- **规则库**：`PermissionStore` 直读直写 `global.json`（数组 {permission,pattern,action}）、`config.json`（bash_mode）、settings.json（trustedFolders）；JSON 用 Qt6::Core QJsonDocument（无新依赖）。
- **询问拦截**：GoalRunner 在 Driving 中以 receivedData 模式匹配 TUI 询问框特征（**特征串由 spike 捕获后固化**）→ 暂停 → `PermissionDialog` → 注入用户应答（方向键/回车序列或 y/n，按 spike 实测）。
- **级别**：goal 对话框二选一；「完全控制」= 显式策略（UI 常驻徽标 + 全审计），「每次询问」= 弹框，超时暂停（禁止自动批）。
- 「本目标总是允许」→ 回写 global.json 该 permission/pattern=allow（持久化，之后会话生效）。

## 12. 主题（B）

- `ThemeManager::apply(Theme::Dark|Light, QWidget* root)`：QSS 文件（`resources/dark.qss`/`light.qss`）+ QPalette 双管齐下；qtermwidget 终端配色经其颜色方案 API 跟随。
- 默认深色（QSettings 无键时取 Dark）；菜单「外观」即时切换 + 广播给所有已建会话终端。

## 13. 开源发布设计（NFR-5）

| 项 | 方案 |
|----|------|
| 仓库/标题 | GitHub `codearts-GUI-ubuntu-x86_64`；LICENSE=MIT（含版权头） |
| 版本 | 语义化 v0.2.0 起；git tag `v0.2.0` 触发 CI |
| 安装包 | CPack：`TGZ`（免安装 tar.gz 含二进制+desktop+install.sh）+ `DEB`（ubuntu-x86_64） |
| CI | `.github/workflows/release.yml`：ubuntu-24.04 → apt Qt6 → 构建+测试 → CPack → gh release 上传 |
| Qt6 可复现 | `-DCODEARTS_QT6_PREFIX=<path>`（默认探测 ChinaSimStdio vcpkg 静态 Qt6；未设且探测失败则 find_package 系统 Qt6——**CMake 显式打印所选来源**，README 记录） |
| 静态闭包 | 仅当 vcpkg 静态 Qt6 时追加 whole-archive 闭包；系统动态 Qt6 走常规链接（if 分支，显式打印） |
| README | 中文：功能/防卡顿与 /goal 架构图/构建（两种 Qt6 来源）/用法/权限说明/免责声明（第三方工具包装） |
| 其他 | `.gitignore`（build/.tmp）、CHANGELOG.md、CONTRIBUTING 简版 |

## 14. 测试策略增补（v2）

| 用例 | 覆盖 |
|------|------|
| GoalState 序列化/断点 | 写→读→字段一致；损坏 JSON → 返回空状态（显式错误，不猜） |
| GoalProtocol 解析 | GOAL-PLAN/STEP/DONE/推荐方案字段解析、错误模式匹配、进度百分比 |
| RetryEngine 状态机 | 错误→立即重试→60s 间隔→计数递增→≥10 警示（fake 时钟注入，不真睡） |
| PermissionStore 读写 | global.json 读/改/回写保留无关字段；bash_mode 改写；非法 JSON 显式失败 |
| Theme 默认深色 | 无设置 → Dark；切换 → 持久化回读 |
| Spike（集成，非常驻） | sendText 注入回显、询问框特征捕获（开发期验证，产物删除） |
