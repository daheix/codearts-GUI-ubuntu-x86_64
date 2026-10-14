# codeartsGUI 任务分解

> 上游：`docs/spec/design/codeartsGUI_design.md`
> 依赖序：T1 构建基座 → T2 环境 → T3 终端会话 → T4 会话管理 → T5 监控 → T6 主窗集成 → T7 测试 → T8 审计

| ID | 任务 | 复杂度 | 验收标准 | 依赖 |
|----|------|--------|----------|------|
| T1 | CMake 基座：静态 Qt6 + FetchContent(lbt+qtermwidget, 含patch) + 传递闭包链接 | 高 | `cmake && build` 通过，产出 codeartsGUI，qtermwidget 静态链入 | — |
| T2 | Environment：codeartsEnv() 装配 OPENCODE_*+TERM，可执行校验 | 中 | doctest：给定 HOME 算出 env；缺失路径返回空 | T1 |
| T3 | SessionWidget：qtermwidget 封装，start/restart，回滚上限5000，信号转发 | 中 | doctest：PID 可得、isRunning 正确、restart 换 PID | T2 |
| T4 | SessionManager：create/close/restart 维护 pid→widget 映射 | 中 | doctest：映射增删正确、restart 后仍可查 | T3 |
| T5 | ProcessMonitor：/proc 采样 RSS/uptime，阈值提示 | 中 | doctest：对自身 pid 采样 RSS>0、uptime 递增 | T3 |
| T6 | MainWindow：QTabWidget 多会话、工具栏/菜单、状态栏(PID/RSS/uptime/健康) | 中 | 冒烟启动、Tab 创建切换、重启按钮生效 | T4,T5 |
| T7 | main.cpp + .desktop + 联调：真实 codearts TUI 冒烟 | 低 | 桌面启动→新建会话→TUI 渲染→键入直通 | T6 |
| T8 | doctest 全量回归 + clang-format + review(security/correctness) 审计 | 低 | 测试全绿、格式化通过、审计无阻断项 | T7 |

## 里程碑
- **M1（可运行基座）**：T1+T2+T3 —— 单会话能启动 codearts TUI 并渲染。
- **M2（多会话+防卡顿）**：T4+T5+T6 —— 多标签、健康监控、一键重启。
- **M3（交付）**：T7+T8 —— 桌面集成、测试审计全绿。

## 风险与回滚
- qtermwidget patch 若 upstream 变更失效 → 回滚到 spike 验证过的 sed/python patch 配方（设计§5）。
- 真实 TUI 登录态依赖 → 单测用 `/bin/cat` 替代，集成冒烟单独标注"需登录态"。
- 静态 Qt6 传递闭包若新增缺符号 → 按 CMakeLists 已记录的 `--whole-archive` 法补库。

---

# 第二期任务（v2，2026-10-14）

| ID | 任务 | 复杂度 | 验收标准 | 依赖 |
|----|------|--------|----------|------|
| T9 | Spike：sendText 注入 TUI（发文本+回车→receivedData 见回复）+ 捕获权限询问框 PTY 特征串 | 高 | 实测日志证明注入回显、特征串落盘；spike 临时产物删除 | ✅ |
| T10 | GoalProtocol（纯逻辑）：GOAL-PLAN/STEP/DONE/推荐方案解析 + 错误模式匹配 | 中 | doctest：样本串解析全对、非法串不崩 | ✅ |
| T11 | GoalState：.goal_state.json 读写/断点恢复/损坏显式失败 | 中 | doctest 往返一致、损坏返回空 | ✅ |
| T12 | RetryEngine：写死参数（立即/60s/∞）+ fake 时钟单测 + 审计日志回调 | 中 | doctest：状态转移/间隔/计数/≥10警示全对 | ✅ |
| T13 | PermissionStore：global.json/bash_mode/trustedFolders 读写（保留无关字段） | 中 | doctest 往返+非法 JSON 显式失败 | ✅ |
| T14 | ThemeManager：dark/light QSS+QPalette，默认深色，QSettings 持久化，终端配色跟随 | 中 | doctest 默认 Dark、切换回读；两 QSS 落盘 | ✅ |
| T15 | GoalRunner 状态机：契约注入/步进/判据核验/恢复注入/询问拦截（依赖 T9 特征） | 高 | doctest 状态转移全绿（mock 注入与输出流） | ✅ |
| T16 | UI：GoalDialog（目标+验收命令+权限级别）、标签进度条、状态栏 goal 区、重试黄条 | 中 | 手测：发起→进度→警示可见 | ✅ |
| T17 | UI：权限管理页（规则表格/bash_mode/信任目录）+ 权限询问弹窗 | 中 | 手测改规则落盘；询问弹窗三选项注入 | ✅ |
| T18 | UI：外观菜单（深色/浅色）+ ThemeManager 接线 | 低 | 手测即时切换、重启保持默认深色/用户选择 | ✅ |
| T19 | 全量测试回归 + clang-format + 审计 v2 | 低 | doctest 全绿、格式化过、audit v2 无阻断 | ✅ |
| T20 | 开源发布：MIT LICENSE/README/CHANGELOG/.gitignore/CPack(TGZ+DEB)/CI release.yml/git tag/GitHub Release | 中 | 本地 CPack 两包产出；CI 语法有效；Release 含安装包+源码 | T19 |

## 第二期里程碑
- **M4（驱动核心）**：T9-T12 —— 协议/状态/重试纯逻辑全测绿。
- **M5（体系集成）**：T13-T18 —— 权限+主题+goal UI 联通，手测通过。
- **M6（交付开源）**：T19-T20 —— 全绿 + GitHub Release。
