# Runtime AI Agent Control Mode

这是 Godot 4.6 自定义分支内的功能说明。页面作为运行时 AI agent 控制模式的长期参考，故意放在官方 `doc/` 文档树之外。

## 摘要

Runtime AI Agent Control Mode 是默认关闭的本机 TCP 控制桥，面向普通项目运行。开启后，外部 AI agent 可以通过 JSONL 命令注入输入、执行带帧等待的连续 batch、截图、读取受限场景树，并在运行过程中开启或停止性能录制。

这个功能刻意保持本机优先：只绑定 `127.0.0.1`，v1 不提供远程鉴权入口，并复用 Godot 现有输入管线和 CLI 性能录制器，而不是把 remote debugger 当成公开 API。

## 快速开始

在普通项目运行中通过 CLI 启用：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --path "E:\Godot Projects\view3d\project" `
  --ai-agent-control `
  --ai-agent-port 7011
```

向本机 TCP 端口发送一行一个 JSON 请求：

```json
{"id":1,"cmd":"ping"}
{"id":2,"cmd":"click","button":"left","x":320,"y":180}
{"id":3,"cmd":"get_screenshot","name":"after_click"}
```

每个响应都包含 `ok` 和 `frame`。成功响应包含 `result`；失败响应包含 `error.code` 和 `error.message`。

## 设置

项目设置位于 `editor/ai_agent_control/*`：

- `enabled`：项目级默认开关。
- `port`：本地 TCP 端口，默认 `7010`。
- `max_batch_ops`：单个 batch 的最大操作数，默认 `1024`。
- `max_line_bytes`：单行 JSONL 请求最大字节数，默认 `1048576`。
- `default_perf_top_frames`：运行时性能录制默认保留的慢帧数量。
- `default_scene_tree_max_depth`：默认场景树深度。
- `screenshot_directory`：PNG 输出目录；为空时使用系统临时目录。

优先级为：

```text
显式 CLI 参数 > 项目设置 > 内建默认值
```

编辑器运行栏也有 AI Agent Control Mode 开关。该开关保存为 editor project metadata，不会直接改写项目设置。如果没有手动切换过，它跟随 `editor/ai_agent_control/enabled`；一旦点击过，运行栏状态就成为该项目的显式运行覆盖项。

## 协议

基础命令：

```json
{"id":1,"cmd":"get_status"}
{"id":2,"cmd":"action","action":"ui_right","pressed":true,"strength":1.0}
{"id":3,"cmd":"key","keycode":"Space","pressed":true}
{"id":4,"cmd":"mouse_button","button":"left","pressed":true,"x":320,"y":180}
{"id":5,"cmd":"mouse_motion","relativeX":10,"relativeY":0}
{"id":6,"cmd":"wait","frames":10}
{"id":7,"cmd":"get_scene_tree","maxDepth":16}
```

同一时间只接受一个 active client。第二个连接会收到 `busy` 并被关闭。超大行返回 `line_too_large`；非法 JSON 返回 `invalid_json`；未知命令返回 `unknown_command`。

## Batch

当 agent 需要确定性的多帧行为时使用 `batch`：

```json
{
  "id":100,
  "cmd":"batch",
  "onError":"stop",
  "ops":[
    {"cmd":"perf_start","name":"drag_probe","topFrames":5},
    {"cmd":"click","button":"left","x":320,"y":180},
    {"cmd":"wait","frames":10},
    {"cmd":"drag","button":"left","from":[320,180],"to":[520,260],"frames":20},
    {"cmd":"get_screenshot","name":"after_drag"},
    {"cmd":"checkpoint","name":"drag_done"},
    {"cmd":"perf_stop"}
  ]
}
```

`checkpoint` 会发送中间响应，然后继续执行。`onError=stop` 会在结束 batch 前释放 AI 持有的 action 和 mouse button。`onError=continue` 会记录错误并继续处理后续操作。

## 高级输入

高级命令会展开成现有 Godot input event：

- `click`：移动、按下、等待、释放。
- `double_click`：两次点击，中间带帧间隔；第二次 press 标记为 double click。
- `hold`：移动、按下、等待、释放。
- `drag`：移动到起点、按下、逐帧插值移动、释放。

鼠标按钮命令维护 AI 自己的 button mask，因此拖动期间的 motion event 会携带正确的按键状态。

## 运行时性能录制

运行时性能命令复用 CLI 性能录制器的 summary 契约，但通过 socket 返回，不写 stdout：

```json
{"id":10,"cmd":"perf_start","name":"probe","topFrames":10}
{"id":11,"cmd":"perf_status"}
{"id":12,"cmd":"perf_stop"}
```

用 summary 快速发现问题。如果后续分析需要逐帧 JSONL 样本，在 `perf_start` 中加入 `samplesFile`。

CLI `--perf-record` 仍然独立工作，并在进程退出时把 summary 打印为 stdout 最后一行。

## 排障

- `busy`：另一个 TCP client 或 batch 正在运行。
- `unknown_action`：请求的 `InputMap` action 不存在。
- `screenshot_unavailable`：当前运行没有可读取的 root viewport texture。
- `perf_not_active`：在 `perf_start` 前发送了 `perf_stop`。
- `line_too_large`：增大 `editor/ai_agent_control/max_line_bytes`，或拆分请求。
- `Get balance request failed! Authentication failed`：这类余额或凭据错误来自项目脚本、插件或外部 API 调用，不是本地 AI 控制桥产生的错误；v1 控制桥没有 token 鉴权，也不会请求外部余额。

## 验证

自动化覆盖位于 `tests/main/test_cli_ai_input_server.h`，重点覆盖 parser、参数校验、mouse button 解析、高级输入展开，以及运行时复用 CLI 性能录制器。

手动验证应覆盖：

- 非 headless 3D 项目的输入和截图。
- headless 下的 action、key、batch、perf 行为。
- 与 `--quit-after` 共存。
- CLI `--perf-record` 与 AI runtime perf 共存。
- editor Run Bar toggle 参数转发。

## 已知限制

- v1 只绑定 `127.0.0.1`。
- v1 没有 token authentication。
- v1 不暴露任意节点属性读取、方法调用、脚本执行或对象修改。
- v1 没有 batch cancel、pause、resume 或 job query 命令。
- 截图响应返回 PNG 文件路径，不返回 base64 payload。

## 相关链接

- [CLI 性能录制器](cli-performance-recorder.md)
- [自定义功能分支流程](../operations/custom-feature-branching.md)
