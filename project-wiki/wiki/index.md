# 项目 Wiki 索引

这个 wiki 保存 Godot 自定义分支的本地知识，避免把自用功能说明混入官方 `doc/` 文档树。

## 自定义功能

- [GDScript LSP CLI](components/gdscript-lsp-cli.md) - 面向自动化的一次性 GDScript LSP 查询和全项目诊断。
- [CLI 性能录制器](components/cli-performance-recorder.md) - CLI 项目运行时的逐帧性能摘要、分位数、慢帧快照和可选 JSONL 样本导出。
- [最新日志 CLI](components/latest-log-cli.md) - 一次性输出最新项目日志，默认只打印尾部行数，适合 AI 快速分析。
- [Runtime AI Agent Control Mode](components/runtime-ai-agent-control.md) - 本机 TCP JSONL 运行时控制桥，支持输入、batch、截图、场景树和 socket 返回的性能探针。

## 操作流程

- [自定义功能分支流程](operations/custom-feature-branching.md) - 每个自定义功能先在独立分支开发，完成后再合并到 `codex/custom-main`。
- [Wiki 日志](log.md) - wiki 更新的时间线记录。
