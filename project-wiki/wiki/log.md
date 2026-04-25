# 项目 Wiki 日志

wiki 活动的追加式记录。

## [2026-04-23] bootstrap | 创建独立项目 wiki

- 创建 `project-wiki/raw/README.md`。
- 创建 `project-wiki/wiki/AGENTS.md`。
- 创建 `project-wiki/wiki/index.md`。
- 创建 `project-wiki/wiki/log.md`。

## [2026-04-23] ingest | GDScript LSP CLI 自定义功能

- 创建 `project-wiki/wiki/components/gdscript-lsp-cli.md`。
- 将分支内 CLI 用法、实现说明、验证结果和已知限制整理到单个功能页面。

## [2026-04-23] ingest | CLI 性能录制器自定义功能

- 创建 `project-wiki/wiki/components/cli-performance-recorder.md`。
- 将 CLI 性能录制用法、输出契约、分位数和慢帧解读、验证说明、当前限制整理到单个功能页面。

## [2026-04-23] ingest | 自定义功能分支流程

- 创建 `project-wiki/wiki/operations/custom-feature-branching.md`。
- 记录仓库规则：每个自定义功能先在独立分支开发，完成和验证后再合并到 `codex/custom-main`。

## [2026-04-23] ingest | Runtime AI agent control 自定义功能

- 创建 `project-wiki/wiki/components/runtime-ai-agent-control.md`。
- 记录运行时 AI 控制桥的渐进式用法、设置、JSONL 协议、batch、高级输入、运行时性能探针、排障、验证和已知限制。

## [2026-04-23] docs | 将 wiki 内容中文化

- 将 wiki 索引、日志、规则页、功能页和流程页改为中文。
- 保留 CLI flag、JSON key、命令示例、文件路径等机器接口原文，避免破坏可复制命令。

## [2026-04-24] ingest | 最新日志 CLI 自定义功能

- 创建 `project-wiki/wiki/components/latest-log-cli.md`。
- 记录 `--latest-log`、`--latest-log-lines`、默认尾部压缩输出、路径解析和轮转日志选择规则。

## [2026-04-24] docs | 最新日志 CLI 语义压缩增强 v2

- 更新 `project-wiki/wiki/components/latest-log-cli.md`。
- 补充 `--latest-log-format <text|json|both|raw>`、默认增强文本摘要、`raw` 兼容模式、问题块识别、保守模板化、数值压缩、相邻重复折叠、栈摘要与 JSON 契约。
- 更新 `project-wiki/wiki/index.md`，将“最新日志 CLI”描述同步到增强版语义。

## [2026-04-24] docs | Runtime AI Agent Control 语义观察与条件同步增强

- 重写 `project-wiki/wiki/components/runtime-ai-agent-control.md`。
- 补充语义观察层、结构化 selector、条件等待原语、高层目标动作、`expect`、`batch_status`、`cancel_batch`、`get_last_error_bundle`、失败证据和新的项目设置默认值。
- 更新 `project-wiki/wiki/index.md`，将 Runtime AI Agent Control 描述同步到“语义观察 + 条件等待 + 高层动作”版本。

## [2026-04-24] docs | 按当前代码实现重新同步自定义 wiki

- 更新 `project-wiki/wiki/index.md`，将“自定义功能”列表改为当前能力总览表。
- 更新 `project-wiki/wiki/components/runtime-ai-agent-control.md`，补充根窗口坐标口径、`viewportPath`、`hasScreenPosition`、`invalid_query`、`invalid_wait`、`target_not_clickable`、观察缓存、截图缓存、Run Bar 参数一致性、最新验证覆盖和 `SubViewport` 相关 warning 限制。
- 复核 `project-wiki/wiki/components/gdscript-lsp-cli.md`、`project-wiki/wiki/components/cli-performance-recorder.md`、`project-wiki/wiki/components/latest-log-cli.md` 与当前代码实现一致，本轮未改动。

## [2026-04-24] docs | 定制功能埋点系统 v1

- 创建 `project-wiki/wiki/components/custom-feature-tracing.md`。
- 更新 `project-wiki/wiki/index.md`，将共享埋点系统加入当前定制功能总览。
- 更新 `project-wiki/wiki/components/gdscript-lsp-cli.md`、`project-wiki/wiki/components/cli-performance-recorder.md`、`project-wiki/wiki/components/latest-log-cli.md`、`project-wiki/wiki/components/runtime-ai-agent-control.md`，补充自动 tracing 行为、典型事件和 sidecar 规则。
- 文档明确当前只有写入端，没有新的读取 CLI。

## [2026-04-25] docs | Runtime AI Agent Control 输入语义修复同步

- 更新 `project-wiki/wiki/components/runtime-ai-agent-control.md`，补充本地 `key` / `mouse_button` / `mouse_motion` 现在会同时驱动 scene-tree 输入分发和 `Input` singleton 的 gameplay 轮询状态。
- 增补“修复了什么”说明，明确这次收敛的是“协议成功但 gameplay 轮询态不同步”的断点。
- 更新验证章节，记录自动化覆盖的 `move_right`、`jump`、`aim`、`shoot`、`mouse_motion` 行为。
- 补录 `E:\GitHub\tps-demo` 的最新手工验证结果，确认菜单进入、移动、跳跃、视角转动、瞄准和射击均已打通。
- 更新 `project-wiki/wiki/index.md`，将 Runtime AI Agent Control 的能力摘要同步到“GUI + gameplay 输入状态同步”版本。
