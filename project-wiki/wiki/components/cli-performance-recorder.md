# CLI 性能录制器

这是 Godot 4.6 自定义分支内的功能说明。页面作为 CLI 性能录制器的长期参考，故意放在官方 `doc/` 文档树之外。

## 摘要

这个功能为会进入主循环的 CLI 项目运行增加性能录制路径。

它记录逐帧时序和 numeric `Performance` monitors，并在 stdout 最后一行打印机器可解析的 JSON summary。

## 快速开始

只需要使用功能时，从这里开始。

项目运行的基础摘要：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --path "E:\Godot Projects\mysterious-museum" `
  --fixed-fps 60 `
  --quit-after 30 `
  --perf-record `
  --perf-end-frame 29
```

在摘要中保留最慢帧：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --path "E:\Godot Projects\mysterious-museum" `
  --fixed-fps 60 `
  --quit-after 30 `
  --perf-record `
  --perf-end-frame 29 `
  --perf-top-frames 5
```

同时导出逐帧 JSONL 样本：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --headless `
  --path "E:\Godot Projects\rush-pet" `
  --fixed-fps 60 `
  --quit-after 30 `
  --perf-record `
  --perf-end-frame 59 `
  --perf-samples-file "E:\GitHub\godot\.omx\rushpet_perf_samples.jsonl"
```

## 常用任务

最常用这些模式：

- 快速健康快照
  `--perf-record --perf-end-frame 59`
- 尖峰排查
  `--perf-record --perf-end-frame 119 --perf-top-frames 10`
- 摘要加原始样本
  `--perf-record --perf-end-frame 119 --perf-samples-file <path>`
- 与 benchmark 共存
  `--benchmark --perf-record --perf-end-frame <n>`

## CLI 接口

### 参数

- `--perf-record`
- `--perf-start-frame <int>`
  默认 `0`。
- `--perf-end-frame <int>`
  启用录制时必填。
- `--perf-top-frames <int>`
  默认 `10`。`0` 表示不输出 `slowFrames`。
- `--perf-samples-file <path>`
  显式开启逐帧样本 JSONL 导出。

### 模式限制

- 支持：会进入主循环的 CLI 项目运行。
- 不支持：editor、project manager、LSP CLI、import/export 或其他一次性 command-line tools。

### 顶层 summary 结构

- `recording`
- `frameMetrics`
- `builtinMonitorMetrics`
- `customMonitorMetrics`
- `budgetSummary`
- `slowFrames`
  除非使用 `--perf-top-frames 0`，否则会出现。

### 单个指标字段

每个指标摘要包含：

- `count`
- `min`
- `max`
- `mean`
- `variance`
- `stddev`
- `first`
- `last`
- `delta`
- `p50`
- `p95`
- `p99`

### 帧指标

固定逐帧指标包括：

- `frame_time_usec`
- `process_time_usec`
- `physics_process_time_usec`
- `navigation_process_time_usec`
- `physics_frame_time_sec`
- `fps`

### 帧预算摘要

`budgetSummary` 用固定阈值统计帧时间超预算情况：

- `over16_667ms`
- `over33_333ms`
- `over50_000ms`

每项包含：

- `thresholdMs`
- `overCount`
- `overRatio`

### 慢帧

`slowFrames` 按 `frame_time_usec` 从最慢到较快排序。

每项包含：

- `frame`
- `frameMetrics`
- `builtinMonitors`
- `customMonitors`

### JSONL 样本结构

`--perf-samples-file` 为每个采样帧写入一个 JSON 对象，包含：

- `frame`
- `frameMetrics`
- `builtinMonitors`
- `customMonitors`
- `recorded`

## 结果解读

当 summary 本身不足以解释问题时阅读这一节。

### 分位数提供什么信息

- `mean` 表示平均成本。
- `p50` 表示典型帧。
- `p95` 和 `p99` 表示是否存在影响体验的尖峰帧。

### `slowFrames` 提供什么信息

`slowFrames` 是不打开完整 trace 时最快查看最差时刻的方式。

适用场景：

- `mean` 看起来正常，但游玩体感不稳定。
- `p95` 或 `p99` 明显差于 `p50`。
- 需要比较尖峰帧上的内建 monitor 值。

### `budgetSummary` 提供什么信息

固定阈值对应常见帧预算：

- `16.667ms` 约等于 60 FPS。
- `33.333ms` 约等于 30 FPS。
- `50.000ms` 约等于 20 FPS。

用 `overRatio` 快速判断帧节奏是否持续超出目标预算。

### 何时使用 samples 文件

当 summary 已经显示问题，但还需要进一步分析时，使用 `--perf-samples-file`：

- 完整逐帧关联。
- 外部绘图。
- 自定义离线分析。

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
  --test-suite="[Main][CLIPerformanceRecorder]"
```

观察结果：

- `7 passed`
- `0 failed`

### 手动冒烟检查

已用以下项目验证：

- `E:\Godot Projects\rush-pet`
  `--headless`、`--benchmark`、`--perf-samples-file`、早于 `end-frame` 退出、`completed=false`
- `E:\Godot Projects\mysterious-museum`
  非 `headless`、`completed=true`、`slowFrames` 有内容、stdout 最后一行保持 JSON

稳定观察到：

- stdout 会输出 summary JSON。
- `budgetSummary` 和 `slowFrames` 存在。
- JSONL 样本导出行数匹配捕获帧数。
- `--benchmark` 输出仍在 summary JSON 之前。

## 已知限制

- 摘要适合发现问题和粗定位，但不是完整的性能轨迹查看器。
- `customMonitorMetrics` 只包含 numeric custom monitors。
- 项目 warning 或 leak report 仍可能在 stdout summary 后出现在 stderr，因此机器解析应只读取 stdout。

## 相关链接

- [项目 Wiki 索引](../index.md)
- [Wiki 日志](../log.md)
