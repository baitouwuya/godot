# 最新日志 CLI

这是 Godot 4.6 自定义分支内的功能说明。页面作为“快速获取最新项目日志”的长期参考，故意放在官方 `doc/` 文档树之外。

## 摘要

`--latest-log` 现在默认不是“原始 tail 读取器”，而是“面向 AI 的语义压缩读取器”。

它仍然只做读取端增强，不改 Godot 全局日志写出格式；但默认会优先抽取最近问题块、折叠相邻重复、压缩仅数值不同的日志、提取关键词，并支持一行 JSON summary。

如果你明确要旧行为，显式传 `--latest-log-format raw`。

## 快速开始

只需要最短可用命令时，从这里开始。

默认增强文本摘要：

```powershell
godot-dev `
  --path "E:\Godot Projects\rush-pet" `
  --latest-log
```

同时要人类可读摘要和机器可读 JSON：

```powershell
godot-dev `
  --path "E:\Godot Projects\rush-pet" `
  --latest-log `
  --latest-log-format both
```

只要最后一行 JSON summary：

```powershell
godot-dev `
  --path "E:\Godot Projects\rush-pet" `
  --latest-log `
  --latest-log-format json
```

回退到旧版原始 tail 行为：

```powershell
godot-dev `
  --path "E:\Godot Projects\rush-pet" `
  --latest-log `
  --latest-log-format raw
```

缩小尾部分析窗口：

```powershell
godot-dev `
  --path "E:\Godot Projects\rush-pet" `
  --latest-log `
  --latest-log-lines 50
```

直接分析整份日志：

```powershell
godot-dev `
  --path "E:\Godot Projects\rush-pet" `
  --latest-log `
  --latest-log-lines 0
```

## 常用任务

- 快速看最近错误和 warning
  `--latest-log`
- 给 agent 一边看文本一边收最后一行 JSON
  `--latest-log --latest-log-format both`
- 只保留机器摘要
  `--latest-log --latest-log-format json`
- 遇到兼容性脚本，强制使用旧 tail 语义
  `--latest-log --latest-log-format raw`
- 收窄上下文窗口
  `--latest-log --latest-log-lines 50`
- 明确要求全文件分析
  `--latest-log --latest-log-lines 0`

## CLI 接口

### 参数

- `--latest-log`
  启用一次性最新日志读取。
- `--latest-log-lines <int>`
  默认 `200`。在增强模式下表示“优先分析的尾部窗口行数”；`0` 表示直接分析整份文件。
- `--latest-log-format <text|json|both|raw>`
  默认 `text`。

### 默认行为

- `--latest-log` 等价于 `--latest-log --latest-log-format text`
- `--latest-log-format raw` 保留 v1 的原始输出语义
- `text|json|both` 都属于增强模式
- 当 `--latest-log-lines N` 且 `N > 0` 时：
  先分析尾部 `N` 行
  如果尾部窗口里没有识别到任何问题块，再自动升级到全文件分析
- `--quiet` 下仍会强制把结果打印到 stdout

### 模式限制

- 支持：普通 CLI 项目上下文
- 不支持：editor、project manager
- 要求：必须能确定项目上下文，也就是使用 `--path <project>`，或者当前工作目录本身就是项目目录

## 输出模式

### `text`

默认模式，输出增强文本摘要。

结构固定为：

1. `[latest-log] path=...`
2. `[latest-log] format=text analyzed=<window>/<total> fallback=<true|false>`
3. `[latest-log] counts error=<n> warning=<n> scriptError=<n> shaderError=<n> repeats=<n>`
4. `[latest-log] keywords ...`
5. `[latest-log] top-templates ...`
6. `Recent issue blocks:` 或 `No issue blocks found; showing folded tail`

### `json`

只输出一行 JSON summary，适合脚本或 agent 直接读取。

### `both`

先输出增强文本摘要，最后一行再输出 JSON summary。

注意：

- 这里保证的是 stdout 最后一行是 JSON summary。
- 运行期如果还有额外 stderr 日志，不影响 stdout 契约。

### `raw`

回退到旧版行为：

```text
[latest-log] path=<resolved-log-path>
[latest-log] lines=<shown>/<total> truncated=true|false
...raw tail or whole file...
```

这个模式不做模板化、关键词提取、问题块识别或 JSON summary。

## 语义压缩规则

需要理解“为什么日志被压缩成这样”时阅读这一节。

### 1. 问题块优先

增强模式会优先识别 Godot 当前稳定的 severity 头：

- `ERROR:`
- `WARNING:`
- `SCRIPT ERROR:`
- `SHADER ERROR:`

一个问题块可包含：

- headline 行
- 后续缩进行
- `at: function (file:line)` 调用点
- `... backtrace (most recent call first):` 头
- 随后的 `[N] func (file:line)` 栈帧

### 2. 保守模板化

只做保守归一化，不做激进模糊匹配。

会归一化：

- 时间戳
- UUID
- IPv4
- 长十六进制值
- 明显动态的长整数
- 普通整数和浮点数

占位符示例：

- `<ts>`
- `<uuid>`
- `<ip4>`
- `<hex>`
- `<int>`
- `<num>`

不会归一化：

- `at:` 行里的 `file:line`
- stack frame 里的 `file:line`
- 函数名
- 资源路径和脚本路径

### 3. 关键数值差异压缩

如果两条相邻日志只有关键数值不同，增强模式会把它们折叠成同一个模板，并为每个 numeric slot 记录：

- `count`
- `min`
- `max`
- `last`

### 4. 相邻重复折叠

只折叠相邻重复，不做跨文件或跨窗口的全局模糊合并。

规则分两层：

- 单行日志：相邻且归一化后完全相同才折叠
- 多行问题块：相邻且 headline/续行模板相同，并且 callsite 与前几帧 stack anchor 也一致才折叠

这意味着：

- 只有数值不同的相邻日志可以压缩
- 多个相邻、结构相同的日志组也可以折叠
- 只要 callsite 或 stack anchor 不同，就不会被错误合并

### 5. 栈摘要与指纹

增强模式会把 `at:` 和 backtrace 解析成结构化字段。

其中：

- `callsite` 返回 `function/file/line`
- `stackSummary.frames` 在 JSON 里最多保留 10 帧
- 文本模式最多显示前 5 帧
- `stackFingerprint` 由 severity、headline 模板、callsite 和前 3 帧 stack 共同生成

### 6. 最近问题块优先

文本模式最多展示最近 3 个问题块。

如果窗口内没有问题块：

- 自动回退到全文件分析
- 如果全文件仍没有问题块，则输出折叠后的尾部预览，而不是直接把 raw tail 原样塞给 AI

### 7. 关键词提取

关键词统计优先基于问题块；如果没有问题块，则基于折叠后的尾部预览。

规则：

- 全部转小写
- 按非字母数字边界切词
- 丢弃长度小于 3 的 token
- 丢弃纯数字 token
- 丢弃停用词和日志噪声词
- `error|warning|script|shader` 不进入关键词统计

## JSON Summary 契约

顶层字段固定为：

- `path`
- `basePath`
- `format`
- `analysis`
- `counts`
- `keywordHits`
- `topTemplates`
- `recentIssueBlocks`
- `fallbackTail`

其中：

- `fallbackTail` 只会在没有识别到任何问题块时出现

### `analysis`

- `windowLinesRequested`
- `windowLinesAnalyzed`
- `totalLines`
- `usedFullFileFallback`
- `foldedRepeatCount`
- `issueBlockCount`

### `counts`

- `error`
- `warning`
- `scriptError`
- `shaderError`

### `topTemplates`

最多保留 5 条。

每项固定包含：

- `templateId`
- `kind`
- `template`
- `count`
- `firstOccurrenceLine`
- `lastOccurrenceLine`
- `numericSlots`

### `numericSlots`

每项固定包含：

- `slot`
- `count`
- `min`
- `max`
- `last`

### `recentIssueBlocks`

最多保留最近 3 个。

每项固定包含：

- `templateId`
- `severity`
- `headline`
- `repeat`
- `callsite`
- `stackFingerprint`
- `stackSummary`

### `callsite`

- `function`
- `file`
- `line`

### `stackSummary`

- `header`
- `frames`

### `frames`

- `index`
- `function`
- `file`
- `line`

## 路径与文件选择

默认行为按下面顺序找日志：

1. 如果传了 `--log-file`，优先使用它作为基础日志路径
2. 否则读取项目设置 `debug/file_logging/log_path`
3. 处理 `user://`、`res://` 和相对路径，得到实际文件系统路径
4. 在该基础路径所在目录里扫描同 basename 和 extension 的日志文件
5. 选择最后修改时间最新的那个文件

这样做的目的不是只读 `godot.log`，而是覆盖轮转后的最新备份文件。

## 实现说明

需要理解边界和维护点时阅读这一节。

### 读取端范围

- 本轮只增强读取端
- 不改 `RotatedFileLogger`
- 不改全局 logger 输出格式
- 不新增 Project Settings

### 内部分析流水线

runner 内部固定分成 4 段：

1. 读取
2. 解析
3. 归一化与折叠
4. 序列化

具体行为：

- `raw` 走旧版尾部/全文读取逻辑
- 增强模式先跑尾部窗口分析
- 尾部无问题块时才回退到全文件
- `text/json/both` 都复用同一份分析结果

## 验证

需要确认功能可信度时阅读这一节。

### 自动检查

单元测试覆盖：

- 参数解析：`--latest-log-format text|json|both|raw`
- 参数校验：非法 format、未启用 `--latest-log` 却传 format
- `raw` 模式兼容
- 双层扫描：尾部命中不回退、尾部 miss 时回退、`--latest-log-lines 0` 直接全文件
- 单行模板化：仅数值不同的相邻日志折叠，非相邻不折叠
- 问题块模板化：相邻相同问题块折叠，callsite 或 stack anchor 不同不折叠
- 数值摘要：`count/min/max/last`
- 栈解析：`at:`、backtrace、fingerprint
- JSON 契约和 `both` 最后一行 JSON

建议测试入口：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --test `
  --test-suite="[Main][CLILatestLogRunner]"
```

本轮实现已实际通过：

```powershell
scons platform=windows target=editor dev_build=yes module_mono_enabled=no tests=yes d3d12=no -j2
.\bin\godot.windows.editor.dev.x86_64.console.exe --test --test-suite="[Main][CLILatestLogRunner]"
```

结果：

- `11 passed`
- `0 failed`

### 手动冒烟

本轮已实际验证：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --path "E:\Godot Projects\rush-pet" `
  --latest-log `
  --latest-log-format both `
  --latest-log-lines 50
```

并额外做了 stdout/stderr 分离检查，确认：

- `both` 模式的 stdout 最后一行是 JSON summary
- 运行期 stderr 日志不会破坏这个 stdout 契约

## 已知限制

- 只做读取端增强，不改变日志源头格式
- 不引入 Drain3、Fluent Bit、Sidecar 或其他新依赖
- 不做 Trace 级摘要，因为当前 Godot 日志没有稳定 trace 上下文
- 只做相邻重复折叠，不做跨窗口或全文件模糊合并
- 模板化优先保证“诊断安全”，不追求最大压缩率

## 相关链接

- [项目 Wiki 索引](../index.md)
- [Wiki 日志](../log.md)
