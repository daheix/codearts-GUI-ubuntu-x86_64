# codeartsGUI 开发计划（SDD 七阶段）

> 目标：自研 Qt6 桌面 GUI 包装 CodeArts CLI/TUI，**核心解决"长会话界面卡顿"**。
> 产物目录：`~/codeartsGUI/docs/spec/{requirements,design,tasks}/`
> Qt6：项目 vcpkg 静态 6.11.2（已验证 configure/编译/链接/启动全通，见 CMakeLists.txt）

## 阶段门

| 阶段 | 产物 | 状态 |
|------|------|------|
| 0 根因定位 | 实验二：卡顿根因（内核层 vs 渲染层）+ PTY 可行性 | ✅ |
| 1 需求 | docs/spec/requirements/codeartsGUI_requirements.md (EARS) | ✅ |
| 2 设计 | docs/spec/design/codeartsGUI_design.md（含防卡顿架构） | ✅ |
| 3 任务 | docs/spec/tasks/codeartsGUI_tasks.md | ✅ |
| 4 探索 | 代码踩点（qtermwidget API spike 已验证） | ✅ |
| 5 实现 | 逐任务编码 + 编译验证 | ✅ |
| 6 测试 | developer-test（doctest 8用例22断言全绿） | ✅ |
| 7 审计 | security/correctness 审计（docs/spec/design/codeartsGUI_audit.md） | ✅ |

## 第二期（v0.2.0）—— /goal 自愈驱动 + 双主题 + 权限体系 + 开源发布

| 阶段 | 内容 | 状态 |
|------|------|------|
| 1 需求 | requirements v2（FR-7~FR-12：goal/双主题/权限/重试/开源） | ✅ |
| 2 设计 | design v2（A~H + 开源发布规划） | ✅ |
| 3 任务 | tasks v2 | ✅ |
| 4 Spike | sendText 注入 + 权限询问框 PTY 特征捕获（T9，发现固化进设计§9） | ✅ |
| 5 实现 | Theme/PermissionStore/GoalRunner/重试引擎/UI（TDD，T10-T18） | ✅ |
| 6 测试 | doctest 全绿（4套件38用例653断言）+ clang-format | ✅ |
| 7 审计 | audit v2（含权限/重试策略审计） | ✅ |
| 8 开源发布 | MIT LICENSE/README/CHANGELOG/.gitignore + packaging/package.sh 打包 + GitHub Release v0.2.0 + CI | ✅ |

**开源项目**：GitHub 仓库 `daheix/codearts-GUI-ubuntu-x86_64`，MIT 协议，Release v0.2.0 含完整安装包（静态自含 tar.gz + .deb）+ 源码 + CI。

## 核心技术挑战：长会话卡顿

**假设清单**（阶段0 验证）：
- H1 渲染层：VS Code webview 输出洪水 → DOM/文本无限增长 → 重绘卡死（自研 Qt 用环形缓冲规避）
- H2 内核层：codearts/opencode 内核长时运行退化（内存泄漏/响应变慢）→ PTY 也会卡
- H3 IO 阻塞：界面主线程阻塞在 PTY 读 → 事件循环饿死

**防卡顿四层架构**（无论根因如何均需内置）：
1. 进程隔离：内核常驻独立进程 + 界面热重连，内核卡/崩不拖垮界面
2. 异步 IO：主线程零阻塞，PTY 输出走后台线程 + 信号槽批量投递
3. 环形缓冲 + 批量渲染：输出洪水限流，只保留最近 N 行，定时合并重绘
4. 内存上限 + 溢出落盘：防长会话内存无限膨胀
