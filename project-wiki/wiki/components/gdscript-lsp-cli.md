# GDScript LSP CLI

这是 Godot 4.6 自定义分支内的功能说明。页面作为一次性 GDScript LSP CLI 的长期参考，故意放在官方 `doc/` 文档树之外。

## 摘要

这个功能提供基于 editor 能力的一次性命令行入口，用于：

- 输出 JSON 的 GDScript LSP 单次查询。
- 全项目 GDScript 诊断。
- 适合自动化读取的快速诊断摘要。
- 可脚本化控制的诊断退出码策略。

默认启动路径优先保持与正常 editor 初始化一致，允许项目插件参与加载；如果需要旧的最小隔离路径，可显式加全局 `--recovery-mode`。

## 快速开始

只需要使用功能时，从这里开始。

运行符号查询：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --headless `
  --path "E:\Godot Projects\view3d\project" `
  --lsp-query document-symbol `
  --file main.gd
```

运行全项目诊断，并在执行完成后始终返回成功：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --headless `
  --path "E:\Godot Projects\view3d\project" `
  --lsp-diagnostics `
  --diagnostics-format summary `
  --diagnostics-fail-on never
```

存在 warning 或 error 时返回失败：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --headless `
  --path "E:\Godot Projects\view3d\project" `
  --lsp-diagnostics `
  --diagnostics-format summary `
  --diagnostics-fail-on warning
```

## 常用任务

最常用这些模式：

- 快速健康检查
  `--lsp-diagnostics --diagnostics-format summary --diagnostics-fail-on never`
- CI 或 agent 门禁
  `--lsp-diagnostics --diagnostics-format summary --diagnostics-fail-on warning`
- 项目相对路径文件查询
  `--lsp-query document-symbol --file scripts/player.gd`
- 完整自定义请求体
  `--lsp-query references --params-json "<json>"`

## CLI 接口

### 入口参数

- `--lsp-query <operation>`
- `--lsp-diagnostics`

### 查询操作

- `hover`
- `definition`
- `declaration`
- `references`
- `document-symbol`
- `completion`
- `signature-help`

### 查询参数

- `--file <path>`
  接受 `res://`、`file://` 或 `main.gd` 这类项目相对路径。
- `--line <line>`
  基于 1 的行号，用于位置相关操作。
- `--column <column>`
  基于 1 的列号，用于位置相关操作。
- `--params-json <json>`
  完整 LSP params 对象。
- `--include-declaration <bool>`
  只对 `references` 有效。

### 诊断参数

- `--diagnostics-format <jsonl|json|summary>`
- `--diagnostics-severity <error|warning|all>`
- `--diagnostics-fail-on <error|warning|any|never>`

### 诊断摘要结构

`summary` 输出一个 JSON 对象，包含：

- `total`
- `bySeverity`
- `byFile`

示例：

```json
{
  "total": 65,
  "bySeverity": {
    "warning": 65
  },
  "byFile": {
    "res://main.gd": 17
  }
}
```

### 退出行为

- 查询成功：退出码 `0`。
- 诊断成功且未触发失败条件：退出码 `0`。
- 诊断成功但触发失败条件：退出码 `1`。
- CLI 参数非法：退出码 `1`。

## 设计说明

只有在需要维护或扩展功能时，才需要读这一节。

### 为什么默认允许插件启动

CLI 默认不再强制 recovery mode，而是使用完整的 editor 启动语义。这样插件项目、tool scripts 和 GDExtension 能按正常 editor 初始化路径参与加载，避免 `--lsp-query` / `--lsp-diagnostics` 因人为禁用插件而报错。

如果确实需要旧的最小隔离路径，继续直接复用全局 `--recovery-mode` 即可。

### 为什么复用现有实现

该功能有意复用现有 GDScript LSP 栈，而不是再实现一套 CLI 专用 LSP 逻辑。

主要运行路径：

- [main/main.cpp](../../../main/main.cpp)
- [modules/gdscript/language_server/gdscript_lsp_cli_runner.h](../../../modules/gdscript/language_server/gdscript_lsp_cli_runner.h)
- [modules/gdscript/language_server/gdscript_lsp_cli_runner.cpp](../../../modules/gdscript/language_server/gdscript_lsp_cli_runner.cpp)
- [modules/gdscript/language_server/gdscript_workspace.cpp](../../../modules/gdscript/language_server/gdscript_workspace.cpp)
- [editor/file_system/editor_file_system.cpp](../../../editor/file_system/editor_file_system.cpp)

### 主要行为变化

- CLI 专用参数解析从 `main` 下沉到 runner。
- 项目相对 `--file` 会规范化为 `res://...`。
- `references` 请求可以注入 `includeDeclaration`。
- 诊断可以输出 summary 对象。
- 诊断退出码由 `--diagnostics-fail-on` 控制。
- 默认会保留插件初始化；仅在显式传入全局 `--recovery-mode` 时才进入最小隔离路径。

## 埋点

当前版本会自动接入共享定制功能埋点系统，不需要额外 CLI flag。

会记录的核心事件包括：

- `startup_options`
- `options_validated` / `options_validation_failed`
- `run_started`
- `query_params`
- `query_result`
- `diagnostics_summary`
- `query_failed`
- `run_finished`

其中：

- 较大的 params / result / diagnostics 会进入 trace payload sidecar
- 失败会带结构化 `error`
- 这些 trace 不会改写原有 stdout JSON 契约

具体落盘位置和会话目录结构见 [定制功能埋点系统](custom-feature-tracing.md)。

## 验证

需要确认功能可信度时阅读这一节。

### 构建和单元检查

本分支使用以下命令验证：

```powershell
scons platform=windows target=editor dev_build=yes module_mono_enabled=no tests=yes d3d12=no -j2
```

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --test `
  --test-suite="[Modules][GDScript][LSP][Editor]" `
  --test-case="*[cli_runner]*"
```

观察结果：

- `6 passed`
- `0 failed`

### 真实项目冒烟测试

依次验证过：

- `E:\Godot Projects\view3d\project`
- `E:\Godot Projects\rush-pet`
- `E:\Godot Projects\mysterious-museum`
- 带 editor plugin 的项目

稳定观察到：

- 诊断 summary 可用。
- diagnostics fail-on 退出码行为可用。
- 项目相对路径文件查询可用。
- 默认模式下不再因为强制 recovery mode 禁用插件。
- 这些项目上的输出保持机器友好。

## 已知限制

- 默认模式下不再人为屏蔽 editor plugins、tool scripts、GDExtension。
- 如果项目插件本身存在真实初始化错误，LSP CLI 仍会像正常 editor 启动一样暴露这些错误，不做吞并。
- 如果需要排除插件副作用或快速隔离问题，可以显式加全局 `--recovery-mode` 退回旧的最小启动路径。

## 后续优化目标

- 在真实插件项目上持续补充手动回归，避免后续把 LSP CLI 启动策略重新收窄回 recovery mode。
- 如果后续需要更细的隔离粒度，再评估是否值得新增比全局 `--recovery-mode` 更窄的显式模式。

## 相关链接

- [项目 Wiki 索引](../index.md)
- [Wiki 日志](../log.md)
- [定制功能埋点系统](custom-feature-tracing.md)
