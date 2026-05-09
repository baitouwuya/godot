# Godot 项目 Harness

这是 Godot 4.6 自定义分支里的项目级自动化 harness。它是编排层，不替代 Runtime AI Agent Control，也不开放新的脚本执行、任意方法调用或任意属性写入能力。

## 摘要

Project Harness v1 用一个 JSON 脚本描述自动化流程，然后在普通项目运行态逐帧执行：

- 调用现有 Runtime AI Agent Control 命令。
- 把高层 alias 展开成已有输入 / wait / batch 命令。
- 对每一步生成结构化 report。
- 失败时收集截图、latest-log 摘要和 trace 证据。

目标是解决“协议返回 `ok=true`，但项目实际没动”的排障断层。Harness 会把每一步的请求、响应、失败原因和证据放在同一份 report 里，方便 AI 或人复盘。

## 快速开始

```powershell
godot-dev --path "E:\GitHub\tps-demo" --windowed --harness-run "res://harness/menu_to_reactor.json"
```

可选输出 report：

```powershell
godot-dev --path "E:\GitHub\tps-demo" `
  --windowed `
  --harness-run "res://harness/menu_to_reactor.json" `
  --harness-report "user://harness/menu_to_reactor.report.json" `
  --harness-format both
```

`--harness-run` 会自动启用 Runtime AI Agent Control 的同进程本地执行器。默认不打开 TCP JSONL 端口；只有显式同时传 `--ai-agent-control` 时，才会额外监听 `127.0.0.1:<port>` 给外部 client。

## CLI 参数

- `--harness-run <path>`
  运行 harness JSON 脚本。路径支持绝对路径、项目相对路径、`res://` 和 `user://`。
- `--harness-report <path>`
  写入 JSON report。未指定时，如果定制 trace 已启用，会写到当前 trace 会话目录下的 `harness-report.json`。
- `--harness-timeout-frames <int>`
  全局超时帧数，默认 `3600`。
- `--harness-format <text|json|both>`
  stdout 输出格式，默认 `text`。

退出码：

- `0`：所有步骤通过。
- `1`：步骤失败或断言失败。
- `2`：CLI 参数或脚本 schema 非法。
- `3`：harness / AI bridge 初始化失败。
- `4`：全局超时。

## 脚本结构

```json
{
  "name": "menu_to_reactor",
  "startup": {
    "windowed": true,
    "aiAgentPort": 7011,
    "perf": false
  },
  "steps": [
    { "cmd": "get_viewport_summary" },
    { "cmd": "click_target", "selector": { "path": "/root/main/Menu/UI/Main/Play" } },
    { "expect": "scene_changed", "timeoutFrames": 300 },
    { "cmd": "hold_action", "action": "move_forward", "frames": 120 },
    { "expect": "node_visible", "selector": { "name": "Reactor" }, "timeoutFrames": 600 }
  ],
  "evidence": {
    "screenshots": "on_failure",
    "latestLog": true,
    "trace": true,
    "perfSummary": false
  }
}
```

当前 schema 严格校验顶层、`startup` 和 `evidence` 字段。每个 step 必须且只能包含一个入口：`cmd` 或 `expect`。

## Step 语义

### 原生命令

`cmd` 可以直接写 Runtime AI Agent Control 已支持的命令，例如：

- `get_status`
- `get_viewport_summary`
- `get_interactables`
- `query_nodes`
- `get_node_snapshot`
- `click_target`
- `focus_target`
- `key`
- `mouse_motion`
- `wait`
- `batch`

Harness 不重新实现这些命令，只把请求交给同进程 AI bridge 执行。

### 动作 alias

当前 v1 提供少量 harness alias：

- `tap_action`
  展开为 `action pressed=true`、`wait`、`action pressed=false`。
- `hold_action`
  展开为按住 action 若干帧后释放。
- `mouse_look`
  展开为 `mouse_motion`，用于视角转动。
- `input_self_test`
  运行输入链路诊断。默认获取 `get_status` 和 `get_viewport_summary`；如果提供 `selector`，会执行 `focus_target`、`key(Enter)`、`key(Space)` 和默认 `click_target`；如果提供 `action`，会执行一次 action press/wait/release；如果提供 `mouseRelative`，会执行一次相对 `mouse_motion`。

`action` 仍然是 gameplay 输入原语，不承诺替代 GUI 点击。GUI 自动化优先使用 `click_target`、`focus_target` 和 `key`。

### 断言 alias

`expect` 会映射到现有等待命令：

- `node_exists` -> `wait_node_exists`
- `node_gone` -> `wait_node_gone`
- `node_visible` -> `wait_property visible == true`
- `node_hidden` -> `wait_property visible == false`
- `property_equals` -> `wait_property`
- `scene_changed` -> `wait_scene_changed`
- `screenshot_diff` -> `wait_screenshot_diff`

等待仍然按帧推进，不阻塞主循环。

## Report

Report 固定包含：

- `name`
- `projectPath`
- `scriptPath`
- `startedAt`
- `endedAt`
- `startedFrame`
- `endedFrame`
- `durationUsec`
- `exitCode`
- `steps`
- `assertions`
- `evidence`
- `failureBundle`
- `traceSessionDir`
- `latestLogSummary`
- `perfSummary`

每个 step 会记录：

- `index`
- `startedFrame`
- `endedFrame`
- `request`
- `response`
- `ok`
- `meta`

失败时 `failureBundle` 会包含错误码、错误信息、原始响应，以及在配置允许时的截图路径。

`expect` step 会额外写入 `assertions`，包含断言 alias、请求、响应、开始/结束帧和通过状态。`evidence.screenshots="always"` 会在成功结束时也保存一张截图；截图失败响应会写入 `failureBundle.screenshotError`。

## Trace 接入

Harness 使用新的 trace feature：

```text
project_harness
```

典型事件：

- `options_validated`
- `harness_started`
- `step_started`
- `step_finished`
- `assertion_failed`
- `harness_finished`

大 report 会通过定制功能埋点系统的 payload sidecar 保存，不塞进主事件行。

## 已知限制

- v1 是读取和编排层，不做项目脚本注入。
- `startup.windowed` 当前只作为脚本意图记录；窗口模式仍建议通过现有 `--windowed` CLI 显式传入。
- `input_self_test` 是输入链路诊断，不等价于项目级胜利条件；真实目标仍应通过 `expect` 明确声明。
- 非 headless 截图依赖 root viewport 图像可读；失败时会在 report 里记录截图失败响应。
- Harness 默认失败即停止，暂不支持 `continueOnFailure`。

## 验证

当前自动测试覆盖：

- CLI 参数解析与上下文校验。
- JSON schema 校验。
- `tap_action`、`hold_action`、`mouse_look`、`node_visible` alias 映射。
- 成功 harness run 的 report 写入。
- `expect` step 写入 `assertions`。
- `project_harness` trace 的 `harness_started` / `harness_finished` 事件。
- Runtime AI Agent Control 的 `[SceneTree]` 回归，确认本地执行入口不破坏现有输入、等待和 trace 行为。
