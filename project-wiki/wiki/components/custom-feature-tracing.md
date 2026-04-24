# 定制功能埋点系统

这是 Godot 4.6 自定义分支里的共享埋点说明。页面只描述当前 v1 写入端，不引入新的读取 CLI。

## 摘要

定制功能埋点系统会在进程运行期间，自动为当前 4 个定制功能写入低开销结构化 trace：

- `gdscript_lsp_cli`
- `cli_perf_recorder`
- `latest_log_cli`
- `runtime_ai_agent_control`

目标不是替代正常 stdout / socket 输出，而是额外保留：

- 功能入口与参数校验
- 核心请求 / 响应
- summary
- 失败与异常
- 生命周期开始与结束

默认自动工作，没有新的 Project Settings，也没有新的读取命令。

## 快速定位

trace 根目录优先使用：

```text
OS::get_singleton()->get_cache_path()/OS::get_singleton()->get_godot_dir_name()/custom_feature_traces
```

如果 `get_cache_path()` 不可用，则回退到：

```text
OS::get_singleton()->get_temp_path()/godot/custom_feature_traces
```

这意味着：

- Windows 下一般会落到当前用户缓存目录
- 不会混入项目目录
- 不会污染正常 `godot.log`

## 会话目录结构

每次进程运行创建一个会话目录，名称格式固定为：

```text
<UTC时间>-pid<PID>-<sessionId>
```

当前目录固定包含：

- `manifest.json`
- `events-0001.jsonl`
- `events-0002.jsonl`
  当事件文件滚动后继续增加
- `payloads/`

示例：

```text
custom_feature_traces/
  2026-04-24T08-31-52Z-pid12345-381203912/
    manifest.json
    events-0001.jsonl
    events-0002.jsonl
    payloads/
      payload-000001-summary.json
      payload-000002-output.txt
```

## `manifest.json`

当前 manifest 固定包含：

- `sessionId`
- `startedAtUtc`
- `endedAtUtc`
- `pid`
- `binaryPath`
- `cwd`
- `projectPath`
- `processMode`
- `featuresSeen`
- `eventFiles`
- `payloadDirectory`

`processMode` 当前固定值：

- `cli_project_run`
- `cmdline_tool`
- `editor_run`

作用：

- 快速定位这次会话是在什么模式下产生
- 判断本次实际触达了哪些定制功能
- 枚举滚动后的事件文件列表

## 事件 JSONL

`events-*.jsonl` 每行一个 JSON 事件，固定字段：

- `seq`
- `tsUnixUsec`
- `monoUsec`
- `sessionId`
- `feature`
- `event`
- `severity`
- `correlationId`
- `data`
- `payloads`
- `error`

其中：

- `feature` 用来区分 4 个定制功能
- `correlationId` 用来串起同一次请求 / 操作链
- `data` 放轻量结构化上下文
- `payloads` 放较大文本 / JSON 的描述符
- `error` 只在失败事件中填充

## Payload 与 Sidecar

`payloads` 是命名对象，值为 payload 描述符。描述符固定字段：

- `mime`
- `bytes`
- `sha256`
- `inline`
- `refPath`

### 分流规则

- `<= 4 KiB` 的文本 / JSON 默认直接写进 `inline`
- `> 4 KiB` 的文本 / JSON 写到 `payloads/` 下的 sidecar 文件，并在事件里只保留 `refPath`
- 对已经存在的大文件不会复制，只记录路径、大小和 hash

这意味着当前 `refPath` 有两种来源：

- 会话内 sidecar 时，通常是相对会话目录的 `payloads/...`
- 引用现有文件时，当前实现会直接记录现有文件的真实路径

v1 不记录二进制原始内容。

## 文件滚动与清理

当前固定策略：

- 单个 `events-*.jsonl` 最大 `8 MiB`
- 超过后自动滚到下一个事件文件
- 会话初始化时保留最近 `30` 个会话目录
- 同时清理超过 `14` 天的旧会话目录

这保证：

- 主事件文件不会无限膨胀
- 老 trace 会自动回收
- 长跑自动化不会把缓存目录撑爆

## 当前记录范围

默认只记录高价值边界事件，不记录高频内部噪声。

### 固定记录

- 功能入口
- 参数解析 / 校验结果
- 初始化成功 / 失败
- 核心请求 / 响应
- summary
- 生命周期结束
- 错误与异常

### 固定不记录

- `CLIPerformanceRecorder` 每帧采样明细
- Runtime AI 每帧 `poll` / `wait` tick
- batch 展开后的每个 micro-op 帧级事件

## 各功能当前接入

### GDScript LSP CLI

当前会记录：

- `startup_options`
- `options_validated` / `options_validation_failed`
- `run_started`
- `query_params`
- `query_result`
- `diagnostics_summary`
- `query_failed`
- `run_finished`

较大的 params / result / diagnostics 会走 payload sidecar。

### CLI 性能录制器

当前会记录：

- `options_validated` / `options_validation_failed`
- `initialized` / `initialize_failed`
- `summary`

如果存在 `--perf-samples-file`，埋点里只引用该文件，不重复复制。

### 最新日志 CLI

当前会记录：

- `run_started`
- `base_log_path_resolved`
- `latest_log_selected`
- `summary`
- `run_finished`
- `base_log_path_failed`
- `latest_log_not_found`
- `build_output_failed`

较大的 `summary` / `output` 会走 sidecar。

### Runtime AI Agent Control

当前会记录：

- `server_started` / `server_shutdown`
- `server_listen_failed`
- `client_connected` / `client_disconnected`
- `request_received`
- `response_sent`
- `busy_rejected`
- `wait_queued`
- `wait_finished`
- `wait_failed`
- `batch_started`
- `batch_checkpoint`
- `batch_finished`
- `batch_step_failed`
- `batch_cancelled`
- `runtime_perf_started`
- `runtime_perf_summary`

较大的 request / response / scene 查询结果 / perf summary 默认走 payload sidecar。

## 错误捕获

当 tracing 会话活跃时，系统会额外挂一个 logger adapter。

当前只捕获 `log_error()` 路径，不镜像普通 `print()`。

会写入 trace 的引擎错误包括：

- `ERROR`
- `WARNING`
- `SCRIPT ERROR`
- `SHADER ERROR`

结构化字段包括：

- `function`
- `file`
- `line`
- `code`
- `rationale`
- script backtrace

对 Runtime AI Agent Control 来说，当前实现还会把延迟 wait / batch / runtime perf stop 重新挂回对应 `correlationId`，避免只有同步请求路径才带上下文。

## 验证

本轮已至少验证：

```powershell
scons platform=windows target=editor dev_build=yes module_mono_enabled=no tests=yes d3d12=no -j2
```

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe --test --test-suite="[Core][CustomFeatureTraceWriter]"
```

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe --test --test-suite="[Main][CustomFeatureTracer]"
```

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe --test --test-suite="[Main][CLILatestLogRunner]"
```

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe --test --test-suite="[Main][CLIAIInputServer][SceneTree]"
```

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe --test --test-case="*[cli_runner]*"
```

## 已知限制

- v1 只有写入端，没有读取 CLI
- 当前默认自动开启，没有独立开关
- 现有文件引用使用 `refPath` 记录真实路径时，消费者需要接受它不是会话内相对路径
- 为保持低开销，v1 故意不记录每帧采样级细节

## 相关链接

- [项目 Wiki 索引](../index.md)
- [GDScript LSP CLI](gdscript-lsp-cli.md)
- [CLI 性能录制器](cli-performance-recorder.md)
- [最新日志 CLI](latest-log-cli.md)
- [Runtime AI Agent Control Mode](runtime-ai-agent-control.md)
