# GDScript LSP CLI

这是 Godot 4.6 自定义分支内的功能说明。页面作为一次性 GDScript LSP CLI 的长期参考，故意放在官方 `doc/` 文档树之外。

## 摘要

这个功能提供基于 editor 能力的一次性命令行入口，用于：

- 输出 JSON 的 GDScript LSP 单次查询。
- 全项目 GDScript 诊断。
- 适合自动化读取的快速诊断摘要。
- 可脚本化控制的诊断退出码策略。

默认启动路径优先降低项目副作用，而不是追求完整 editor 环境一致性。

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

### 为什么默认使用最小启动路径

CLI 默认使用基于 recovery-mode 语义的最小 editor 启动路径。这样能减少自动化运行时项目 editor plugin 的副作用。

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
- recovery-mode 启动下跳过首次扫描阶段的插件初始化。

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

- `3 passed`
- `0 failed`

### 真实项目冒烟测试

依次验证过：

- `E:\Godot Projects\view3d\project`
- `E:\Godot Projects\rush-pet`
- `E:\Godot Projects\mysterious-museum`

稳定观察到：

- 诊断 summary 可用。
- diagnostics fail-on 退出码行为可用。
- 项目相对路径文件查询可用。
- 这些项目上的输出保持机器友好。

## 已知限制

`E:\Godot Projects\PluginTest` 仍暴露一个 editor 侧边界问题。

当前观察到的行为：

- 诊断 summary JSON 仍可能成功打印。
- 查询 JSON 仍可能成功打印。
- stderr 仍可能出现来自 `res://addons/tripo-godot/editor/*.tscn` 的 `Parse Error: Busy`。
- 由于这些 editor 资源加载失败，进程退出码仍可能变为 `1`。

这说明最小启动路径已经足够支持多个真实项目，但还没有完全隔离所有插件较重的 editor 资源路径。

## 后续优化目标

- 追踪 `PluginTest` 中哪条 editor 资源加载路径仍会触达 `addons/tripo-godot/editor/*.tscn`。
- 保持当前最小启动路径作为默认行为。
- 如果未来需要 full-editor 模式，只作为显式 opt-in 增加。

## 相关链接

- [项目 Wiki 索引](../index.md)
- [Wiki 日志](../log.md)
