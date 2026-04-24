# Runtime AI Agent Control Mode

这是 Godot 4.6 自定义分支里的运行时 AI agent 桥说明。页面单独放在项目 wiki 中，不混入官方 `doc/` 树。

## 摘要

Runtime AI Agent Control Mode 是默认关闭的本机 TCP JSONL 控制桥，面向普通项目运行态。

当前版本已经从“低层输入桥”扩展到“可稳定自动化的运行时 agent 桥”，并在最新实现里补齐了观察口径、缓存复用和严格错误语义，覆盖三层能力：

- 低层输入：`action`、`key`、`mouse_button`、`mouse_motion`、`wait`
- 语义观察：`get_interactables`、`query_nodes`、`get_node_snapshot`、`get_viewport_summary`
- 条件同步与高层动作：`wait_*`、`click_target`、`drag_target_to_target`、`type_text`、`batch_status`、`cancel_batch`

边界保持严格受限：

- 只绑定 `127.0.0.1`
- 不开放任意脚本执行
- 不开放任意方法调用
- 不开放任意属性写入
- 动作仍复用 Godot 现有输入管线

## 快速开始

CLI 运行项目时显式开启：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --path "E:\Godot Projects\VFX-sketchbook-Godot-4.x" `
  --ai-agent-control `
  --ai-agent-port 7011
```

连接后，一行一个 JSON 请求：

```json
{"id":1,"cmd":"ping"}
{"id":2,"cmd":"get_status"}
{"id":3,"cmd":"get_viewport_summary"}
{"id":4,"cmd":"query_nodes","maxResults":5}
```

成功响应固定包含：

- `ok`
- `frame`
- `result`

失败响应固定包含：

- `ok`
- `frame`
- `error.code`
- `error.message`

## 设置

项目设置位于 `editor/ai_agent_control/*`：

- `enabled`
  项目级默认开关
- `port`
  本地 TCP 端口，默认 `7010`
- `max_batch_ops`
  单个 batch 最大操作数，默认 `1024`
- `max_line_bytes`
  单行 JSONL 请求上限，默认 `1048576`
- `default_perf_top_frames`
  runtime perf 默认 slow frame 数，默认 `10`
- `default_scene_tree_max_depth`
  `get_scene_tree` 默认深度，默认 `32`
- `default_wait_timeout_frames`
  等待类命令默认超时帧数，默认 `240`
- `default_poll_every_frames`
  等待类命令默认轮询间隔，默认 `1`
- `default_capture_on_error`
  失败时默认是否附带证据，默认 `false`
- `default_query_max_results`
  查询类命令默认结果上限，默认 `128`
- `screenshot_directory`
  截图目录；为空时使用系统临时目录

优先级：

```text
显式 CLI 参数 > Project Settings > 内建默认值
```

编辑器 Run Bar 也有 AI Agent Control Mode 开关。这个开关保存为 editor project metadata，是运行便利覆盖项，不直接改写 `enabled`。

当前实现还保证：Run Bar 开启 AI Agent Control 后，editor/plugin 能看到的 run args 与实际子进程启动参数保持一致。

## 协议基础

传输规则：

- TCP JSONL
- 单 client
- 单 active batch
- 请求可选 `id`
- 响应原样回传 `id`

基础命令：

```json
{"id":1,"cmd":"get_status"}
{"id":2,"cmd":"action","action":"ui_right","pressed":true,"strength":1.0}
{"id":3,"cmd":"key","keycode":"Space","pressed":true}
{"id":4,"cmd":"mouse_button","button":"left","pressed":true,"x":320,"y":180}
{"id":5,"cmd":"mouse_motion","relativeX":10,"relativeY":0}
{"id":6,"cmd":"wait","frames":10}
{"id":7,"cmd":"get_screenshot","name":"after_click"}
{"id":8,"cmd":"get_scene_tree","maxDepth":16}
```

协议错误：

- `busy`
- `line_too_large`
- `invalid_json`
- `unknown_command`

## 语义观察

### 1. `get_viewport_summary`

返回当前运行态的高层概览：

- `windowSize`
- `rootPath`
- `rootScene`
- `activeCamera`
- `uiTargets`
- `interactableUiTargets`
- `targets3D`
- `interactableTargets3D`

示例：

```json
{"id":10,"cmd":"get_viewport_summary"}
```

### 2. `query_nodes`

按结构化 filter 查询节点，不引入字符串 DSL。

支持的过滤字段：

- `path`
- `name`
- `type`
- `group`
- `text`
- `is3D`
- `visible`
- `screenRect`
- `nearestToScreenPoint`

示例：

```json
{"id":11,"cmd":"query_nodes","filter":{"group":"menu"},"maxResults":20}
{"id":12,"cmd":"query_nodes","filter":{"name":"Turner","is3D":true}}
{"id":13,"cmd":"query_nodes","filter":{"nearestToScreenPoint":[640,360],"is3D":true}}
```

### 3. `get_interactables`

返回当前可交互目标列表，统一覆盖 UI 与常见 3D 可见目标。

示例：

```json
{"id":14,"cmd":"get_interactables","maxResults":20}
```

### 4. `get_node_snapshot`

按 selector 解析单个目标，并返回白名单快照。

当 selector 带 `path` 时，当前实现优先直接解析该节点，不先做全树扫描。

示例：

```json
{"id":15,"cmd":"get_node_snapshot","selector":{"name":"PlayButton"}}
```

### 统一目标结构

当前统一目标快照固定包含：

- `nodePath`
- `name`
- `type`
- `groups`
- `visible`
- `disabled`
- `is3D`
- `screenRect`
- `screenPoint`
- `hasScreenPosition`
- `viewportPath`
- `worldPosition`
- `text`
- `value`
- `selected`
- `childCount`

口径固定为：

- `screenRect` / `screenPoint` 统一按根窗口坐标系返回
- `viewportPath` 表示该快照来自哪个 viewport
- `hasScreenPosition` 表示当前是否真的拿到了可用屏幕位置
- 3D 目标允许返回近似 `screenRect`；如果无法稳定给出包围盒，至少返回 `screenPoint`

取不到的字段返回 `null`，不会因为单个字段缺失让整个查询失败。

## 观察口径与缓存

当前实现的观察层已经做了稳定性与开销收敛：

- 遍历场景树时会持续跟踪节点所属的有效 viewport 和 camera，而不是始终复用 root viewport / root camera
- `screenRect` / `screenPoint` 先在节点本地 viewport 中求值，再统一映射到根窗口坐标系
- `nearestToScreenPoint` 只会在 `hasScreenPosition=true` 的候选里参与匹配，没有屏幕坐标的节点会被排除
- 同一帧最多构建一次 observation snapshot，`get_interactables`、`query_nodes`、`get_node_snapshot`、`wait_*`、高层 target 动作都会复用它
- 同一帧最多做一次 root viewport 图像读回，`wait_screenshot_diff` 和 `captureOnError` 会共享截图缓存
- 路径型查询和等待优先走直接节点查找，减少大场景下的整树扫描

这些优化只改变稳定性和性能，不改变现有 JSONL 协议面。

## 条件同步

等待类命令用于替代“猜多少帧”：

- `wait_until`
- `wait_node_exists`
- `wait_node_gone`
- `wait_property`
- `wait_scene_changed`
- `wait_screenshot_diff`

通用字段：

- `timeoutFrames`
- `pollEveryFrames`
- `captureOnError`

### 直接等待命令

```json
{"id":20,"cmd":"wait_node_exists","selector":{"name":"PlayButton"},"timeoutFrames":180}
{"id":21,"cmd":"wait_property","selector":{"name":"StatusLabel"},"property":"text","equals":"Ready"}
{"id":22,"cmd":"wait_scene_changed","fromScenePath":"/root/Menu"}
{"id":23,"cmd":"wait_screenshot_diff","threshold":0.08}
```

`wait_property` 的比较口径固定为：

- `visible` / `disabled` / `selected` 使用 bool
- `text` 使用 string
- `value` 使用 int / float 数值

### 通用 `wait_until`

`wait_until` 使用受限 `condition` 对象：

```json
{
  "id": 24,
  "cmd": "wait_until",
  "condition": {
    "kind": "property",
    "selector": { "name": "StatusLabel" },
    "property": "text",
    "equals": "Done"
  },
  "timeoutFrames": 240
}
```

当前 `kind`：

- `node_exists`
- `node_gone`
- `property`
- `scene_changed`
- `screenshot_diff`

超时返回 `timeout`，不会阻塞主循环。

## 参数校验与失败语义

当前版本不再把参数类型错误静默折叠成“查不到目标”：

- selector / filter 只接受 `path`、`name`、`type`、`group`、`text`、`is3D`、`visible`、`screenRect`、`nearestToScreenPoint`
- selector / filter 字段类型错误、未知字段、`maxResults < 1` 等问题返回 `invalid_query`
- wait 参数类型错误、非法 `timeoutFrames` / `pollEveryFrames`、不支持的 `wait_property.property`、`equals` 类型不匹配等问题返回 `invalid_wait`
- `nearestToScreenPoint` 必须是 `[x, y]` 数组，`screenRect` 必须是对象
- 错误响应仍可能附带 `lastResolvedTarget`、`sceneDigest`、`screenshotPath`

## 高层动作

高层动作先用 selector 解析目标，再复用现有输入注入。

支持字段：

- `path`
- `name`
- `type`
- `group`
- `text`
- `is3D`
- `nearestToScreenPoint`

当前命令：

- `click_target`
- `double_click_target`
- `focus_target`
- `hover_target`
- `drag_target_to_target`
- `type_text`
- `scroll_view`

示例：

```json
{"id":30,"cmd":"hover_target","selector":{"name":"Turner","is3D":true}}
{"id":31,"cmd":"click_target","selector":{"name":"PlayButton"}}
{"id":32,"cmd":"double_click_target","selector":{"name":"InventorySlot"}}
{
  "id": 33,
  "cmd": "drag_target_to_target",
  "fromSelector": { "name": "ItemA" },
  "toSelector": { "name": "SlotB" },
  "frames": 20
}
{"id":34,"cmd":"type_text","selector":{"name":"SearchBox"},"text":"fireball"}
{"id":35,"cmd":"scroll_view","selector":{"name":"ItemList"},"direction":"down","steps":3}
```

动作响应会带回：

- `resolvedTarget` 或 `resolvedFromTarget` / `resolvedToTarget`
- 实际使用的 `position` 或 `from` / `to`
- `expandedSteps`

找不到目标返回 `target_not_found`；匹配多个目标返回 `ambiguous_target`；命中了节点但没有可用屏幕坐标时返回 `target_not_clickable`。

## 动作后确认

两个快捷命令已经内置“动作 + 等待”：

- `click_target_and_wait`
- `type_text_and_wait`

示例：

```json
{
  "id": 40,
  "cmd": "click_target_and_wait",
  "selector": { "name": "PlayButton" },
  "wait": {
    "condition": {
      "kind": "scene_changed",
      "fromScenePath": "/root/Menu"
    },
    "timeoutFrames": 240
  }
}
```

这里的 `wait` 本质上就是追加一个 `expect` 步骤，判断逻辑与等待原语完全复用，不会分裂成第二套确认系统。

## Batch 与调试

### Batch

```json
{
  "id": 100,
  "cmd": "batch",
  "onError": "stop",
  "ops": [
    {"cmd":"click_target","selector":{"name":"PlayButton"}},
    {"cmd":"expect","condition":{"kind":"scene_changed","fromScenePath":"/root/Menu"}},
    {"cmd":"perf_start","name":"menu_to_game"},
    {"cmd":"drag_target_to_target","fromSelector":{"name":"CardA"},"toSelector":{"name":"Slot1"},"frames":18},
    {"cmd":"checkpoint","name":"drag_done"},
    {"cmd":"perf_stop"}
  ]
}
```

规则：

- 单 active batch
- `onError` 支持 `stop` / `continue`
- `checkpoint` 会先发事件，再继续后续步骤
- `expect` 复用 `wait_until`

### 调试命令

- `batch_status`
- `cancel_batch`
- `list_checkpoints`
- `get_last_error_bundle`

示例：

```json
{"id":110,"cmd":"batch_status"}
{"id":111,"cmd":"cancel_batch"}
{"id":112,"cmd":"list_checkpoints"}
{"id":113,"cmd":"get_last_error_bundle"}
```

### 失败证据

错误响应会尽量附带：

- `lastResolvedTarget`
- `sceneDigest`
- `screenshotPath`

其中截图只在 `captureOnError=true` 或项目默认启用该项时尝试生成。

## 运行时性能录制

runtime perf 仍复用现有 `CLIPerformanceRecorder`：

```json
{"id":120,"cmd":"perf_start","name":"probe","topFrames":10}
{"id":121,"cmd":"perf_status"}
{"id":122,"cmd":"perf_stop"}
```

与 CLI `--perf-record` 的差异：

- runtime perf 结果经 socket 返回
- CLI perf summary 仍保留 stdout 最后一行契约

两者可以共存。

## 验证

自动化：

- `tests/main/test_cli_ai_input_server.h`
- 当前已覆盖 `SubViewport + secondary camera + root camera` 观察口径
- 当前已覆盖 `nearestToScreenPoint` 稳定命中
- 当前已覆盖 `invalid_query`
- 当前已覆盖 `invalid_wait`
- 当前已覆盖 `wait_property` 类型归一化
- 当前已覆盖 observation / screenshot cache reuse
- 当前已覆盖 batch 调试状态
- 当前已覆盖 editor run args 接线一致性

构建：

```powershell
scons platform=windows target=editor dev_build=yes module_mono_enabled=no tests=yes d3d12=no -j2
```

自动测试：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe --test --test-suite="[Main][CLIAIInputServer][SceneTree]"
```

本轮手工烟雾验证：

- `E:\Godot Projects\view3d\project`
- 已确认 `get_viewport_summary` 与 `query_nodes` 在真实项目中正常返回

## 排障

- `busy`
  另一个 client 或 batch 正在运行
- `invalid_query`
  selector / filter 字段名、字段类型或 `maxResults` 非法
- `invalid_wait`
  wait 参数、`wait_property` 比较值或 `condition` 对象非法
- `target_not_found`
  selector 没有命中目标
- `ambiguous_target`
  selector 命中多个目标，需要更具体
- `target_not_clickable`
  selector 命中了节点，但当前没有可用屏幕坐标，无法安全注入点击
- `timeout`
  等待条件在给定帧窗口内未满足
- `screenshot_unavailable`
  当前运行没有可读取的 viewport texture
- `line_too_large`
  请求行超过 `max_line_bytes`
- `Get balance request failed! Authentication failed`
  这类外部 API 认证错误来自项目脚本、插件或网络服务，不是本地 AI control bridge 自身逻辑

## 已知限制

- 仍然只支持本机 `127.0.0.1`
- 仍然不开放任意节点方法调用
- 仍然不开放任意属性写入
- 3D `screenRect` 目前允许近似包围盒
- 某些 `SubViewport` 如果不是 `SubViewportContainer` 的子节点，Godot 运行时仍可能出现 `get_screen_transform` 相关 warning；这是当前引擎布局限制，不是 AI bridge 新增错误
- `pause / resume` 还没有实现
- `type_text` 当前优先覆盖常见文本输入路径，不等同于完整 IME 仿真

## 相关链接

- [CLI 性能录制器](cli-performance-recorder.md)
- [自定义功能分支流程](../operations/custom-feature-branching.md)
