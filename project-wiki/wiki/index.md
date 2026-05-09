# 项目 Wiki 索引

这个 wiki 保存 Godot 自定义分支的本地知识，避免把自用功能说明混入官方 `doc/` 文档树。

## 当前定制功能总览

| 功能 | 主要入口 | 当前能力 | 说明页 |
| --- | --- | --- | --- |
| GDScript LSP CLI | `--lsp-query` `--lsp-diagnostics` | 一次性 GDScript LSP 查询、全项目诊断、summary 输出、可控退出码，默认保留完整 editor 启动语义并允许插件参与初始化 | [GDScript LSP CLI](components/gdscript-lsp-cli.md) |
| CLI 性能录制器 | `--perf-record` | CLI 项目运行时的逐帧性能摘要、分位数、慢帧快照、budget 统计和可选 JSONL 样本导出 | [CLI 性能录制器](components/cli-performance-recorder.md) |
| 最新日志 CLI | `--latest-log` | 默认输出面向 AI 的语义压缩日志摘要，支持 `text`、`json`、`both`、`raw`，并带问题块、重复折叠、关键词和栈摘要 | [最新日志 CLI](components/latest-log-cli.md) |
| Runtime AI Agent Control Mode | `--ai-agent-control`、Project Settings、Run Bar 开关 | 本机 TCP JSONL 运行时 agent 桥，支持语义观察、条件等待、高层目标动作、batch 调试、socket 返回的性能探针，并已补齐观察缓存、截图缓存、严格错误语义，以及本地 key/mouse 对 scene-tree 分发和 `Input` gameplay 状态的同步 | [Runtime AI Agent Control Mode](components/runtime-ai-agent-control.md) |
| 定制功能埋点系统 | 默认自动开启，无独立读取 CLI | 为当前定制功能自动写入共享结构化 trace，会话目录包含 `manifest.json`、滚动 `events-*.jsonl` 和 `payloads/`，支持 sidecar、外部文件引用和错误捕获 | [定制功能埋点系统](components/custom-feature-tracing.md) |
| Godot 项目 Harness | `--harness-run` | 项目级脚本化自动化编排层，复用 Runtime AI Agent Control 执行动作和等待断言，生成 report、trace、截图和 latest-log 证据，方便复现“命令成功但项目没动”的问题 | [Godot 项目 Harness](components/project-harness.md) |

## 操作流程

- [自定义功能分支流程](operations/custom-feature-branching.md) - 每个自定义功能先在独立分支开发，完成后再合并到 `codex/custom-main`。
- [Wiki 日志](log.md) - wiki 更新的时间线记录。
