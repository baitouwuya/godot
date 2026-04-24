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
