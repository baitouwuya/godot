# 最新日志 CLI

这是 Godot 4.6 自定义分支内的功能说明。页面作为“快速获取最新项目日志”的长期参考，故意放在官方 `doc/` 文档树之外。

## 摘要

这个功能增加了一条一次性 CLI 命令：`--latest-log`。

它会在项目上下文内解析当前生效的日志路径，自动挑选最新的日志文件，并默认只输出最后 `200` 行，避免把整份日志一次性塞进 AI 上下文。

## 快速开始

只需要用法时，从这里开始。

查看最新日志的压缩尾部：

```powershell
godot-dev `
  --path "E:\Godot Projects\view3d" `
  --latest-log
```

只看最后 `50` 行：

```powershell
godot-dev `
  --path "E:\Godot Projects\view3d" `
  --latest-log `
  --latest-log-lines 50
```

确实需要完整日志时再显式展开：

```powershell
godot-dev `
  --path "E:\Godot Projects\view3d" `
  --latest-log `
  --latest-log-lines 0
```

## 常用任务

最常用的是这几种：

- 快速给 AI 看最近错误
  `--latest-log`
- 缩到更短的上下文
  `--latest-log --latest-log-lines 50`
- 明确要全量日志
  `--latest-log --latest-log-lines 0`
- 安静模式下也强制打印结果
  `--quiet --latest-log`

## CLI 接口

### 参数

- `--latest-log`
  启用一次性最新日志读取。
- `--latest-log-lines <int>`
  默认 `200`。`0` 表示输出完整文件。

### 模式限制

- 支持：普通 CLI 项目上下文。
- 不支持：editor、project manager。
- 要求：必须能确定项目上下文，也就是使用 `--path <project>`，或者当前工作目录本身就是一个项目目录。

### 输出格式

stdout 会输出两段元信息，然后输出日志内容：

```text
[latest-log] path=<resolved-log-path>
[latest-log] lines=<shown>/<total> truncated=true|false
...log tail...
```

说明：

- `path` 是最终被选中的最新日志文件，而不是单纯的基础配置路径。
- `lines` 表示本次打印了多少行，以及文件总共有多少行。
- 只有在输出被截断时，第二行才会附带 `truncated=true`。

## 路径与文件选择

默认行为按下面顺序找日志：

1. 如果传了 `--log-file`，优先使用它作为基础日志路径。
2. 否则读取项目设置 `debug/file_logging/log_path`。
3. 处理 `user://`、`res://` 和相对路径，得到实际文件系统路径。
4. 在该基础路径所在目录里扫描同 basename 和 extension 的日志文件。
5. 选择最后修改时间最新的那个文件。

这样做的目的不是只读 `godot.log`，而是能覆盖轮转后的最新备份文件。

## 为什么默认只输出尾部

这是面向 AI 分析的默认策略。

大多数排错场景真正有价值的是最近几十到几百行，而不是完整历史。默认只取尾部有几个直接收益：

- 降低上下文体积，避免 prompt 被旧日志淹没。
- 保留最近一次报错、warning、栈追踪和资源加载失败信息。
- 不需要每次都手动在外部再做一次 `tail`。

如果需要完整文件，显式传 `--latest-log-lines 0` 即可。

## 实现说明

需要理解行为边界时阅读这一节。

- 这是只读功能，不改变现有日志写入、轮转或 Project Settings 行为。
- 命令在一次性 CLI 路径中执行，不进入主循环。
- 默认尾部读取采用流式逐行读取，只保留最后 N 行，不把整份大日志读进结果缓冲。
- `--quiet` 不会压掉本命令输出；runner 会临时打开 stdout，确保日志结果可见。

## 验证

需要确认功能可信度时阅读这一节。

### 自动检查

单元测试覆盖：

- 参数解析：`--latest-log`、`--latest-log-lines`
- 参数校验：未启用 `--latest-log` 却传 `--latest-log-lines`、负数行数、缺少项目上下文
- 路径解析：`user://` 和项目相对路径
- 最新文件选择：当前日志与轮转日志
- 读取策略：尾部读取和完整读取

建议的测试入口：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --test `
  --test-suite="[Main][CLILatestLogRunner]"
```

本轮实现已实际通过：

- `scons platform=windows target=editor dev_build=yes module_mono_enabled=no tests=yes d3d12=no -j2`
- `.\bin\godot.windows.editor.dev.x86_64.console.exe --test --test-suite="[Main][CLILatestLogRunner]"`

结果：

- `5 passed`
- `0 failed`

### 手动冒烟

建议至少验证两类命令：

- 项目目录下直接执行 `--latest-log`
- 显式 `--path <project>` 执行 `--latest-log --latest-log-lines 50`

观察点：

- 能输出 `[latest-log] path=...`
- 默认不会把整份日志全部打出来
- `--latest-log-lines 0` 会切到完整输出
- `--quiet` 下仍然能在 stdout 拿到结果

本轮手动验证项目：

- `E:\Godot Projects\rush-pet`

已观察到：

- `--path "E:\Godot Projects\rush-pet" --latest-log --latest-log-lines 5` 会输出路径和压缩尾部。
- `--path "E:\Godot Projects\rush-pet" --quiet --latest-log --latest-log-lines 5` 在 stdout 中仍然保留 `[latest-log] ...` 元信息和日志尾部。

## 已知限制

- 这个命令只做“最新日志文件读取”，不做多文件聚合。
- “最新”依据是最后修改时间，而不是日志文件名排序。
- 默认压缩只保留尾部行，不会自动提取 error/warning 关键词摘要。

## 相关链接

- [项目 Wiki 索引](../index.md)
- [Wiki 日志](../log.md)
