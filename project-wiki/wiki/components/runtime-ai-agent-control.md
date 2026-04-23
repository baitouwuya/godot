# Runtime AI Agent Control Mode

Branch-local custom feature for this Godot 4.6 repository. This page is the
single durable reference for runtime AI agent control and stays outside the
official `doc/` tree on purpose.

## Summary

Runtime AI Agent Control Mode is a default-off local TCP bridge for project
runs. When enabled, an external AI agent can send JSONL commands to inject
input, run multi-step batches with frame waits, capture screenshots, inspect a
bounded scene tree, and start or stop runtime performance recording.

The feature is intentionally local-first: it binds to `127.0.0.1`, has no v1
remote auth surface, and reuses Godot's existing input and CLI performance
recorder paths instead of exposing the remote debugger as a public API.

## Quick Start

Enable from CLI for a normal project run:

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --path "E:\Godot Projects\view3d\project" `
  --ai-agent-control `
  --ai-agent-port 7011
```

Send one JSON request per line to the local TCP port:

```json
{"id":1,"cmd":"ping"}
{"id":2,"cmd":"click","button":"left","x":320,"y":180}
{"id":3,"cmd":"get_screenshot","name":"after_click"}
```

Every response includes `ok` and `frame`. Successful responses include
`result`; failures include `error.code` and `error.message`.

## Settings

Project Settings live under `editor/ai_agent_control/*`:

- `enabled`: project-level default switch.
- `port`: local TCP port, default `7010`.
- `max_batch_ops`: maximum operations in one batch, default `1024`.
- `max_line_bytes`: maximum JSONL request size, default `1048576`.
- `default_perf_top_frames`: default runtime perf slow-frame count.
- `default_scene_tree_max_depth`: default scene tree depth.
- `screenshot_directory`: PNG output directory; empty uses the OS temp path.

Priority is:

```text
explicit CLI arguments > Project Settings > built-in defaults
```

The editor Run Bar also has an AI Agent Control Mode toggle. The toggle is saved
as editor project metadata and does not rewrite Project Settings. If no toggle
override exists, it follows `editor/ai_agent_control/enabled`; once clicked, the
Run Bar state becomes the explicit launch override for that project.

## Protocol

Basic commands:

```json
{"id":1,"cmd":"get_status"}
{"id":2,"cmd":"action","action":"ui_right","pressed":true,"strength":1.0}
{"id":3,"cmd":"key","keycode":"Space","pressed":true}
{"id":4,"cmd":"mouse_button","button":"left","pressed":true,"x":320,"y":180}
{"id":5,"cmd":"mouse_motion","relativeX":10,"relativeY":0}
{"id":6,"cmd":"wait","frames":10}
{"id":7,"cmd":"get_scene_tree","maxDepth":16}
```

Only one active client is accepted. A second connection receives `busy` and is
closed. Oversized lines return `line_too_large`; invalid JSON returns
`invalid_json`; unknown commands return `unknown_command`.

## Batch

Use `batch` when an agent needs deterministic multi-frame behavior:

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

`checkpoint` emits an intermediate response and then continues. `onError=stop`
releases AI-held actions and mouse buttons before ending the batch.
`onError=continue` records errors and keeps processing later operations.

## Advanced Input

High-level commands expand into existing Godot input events:

- `click`: move, press, wait, release.
- `double_click`: two clicks with a frame gap; the second press is marked as a
  double click.
- `hold`: move, press, wait, release.
- `drag`: move to start, press, per-frame motion interpolation, release.

Mouse button commands maintain an AI-owned button mask, so drag motion carries
the correct pressed-button state.

## Runtime Perf

Runtime perf commands use the same summary contract as the CLI performance
recorder but return through the socket instead of stdout:

```json
{"id":10,"cmd":"perf_start","name":"probe","topFrames":10}
{"id":11,"cmd":"perf_status"}
{"id":12,"cmd":"perf_stop"}
```

Use summary output for quick problem discovery. Add `samplesFile` to
`perf_start` when a follow-up analysis needs per-frame JSONL samples.

CLI `--perf-record` remains independent and still prints its summary as the last
stdout line on process exit.

## Troubleshooting

- `busy`: another TCP client or batch is active.
- `unknown_action`: the requested `InputMap` action does not exist.
- `screenshot_unavailable`: the run has no readable root viewport texture.
- `perf_not_active`: `perf_stop` was sent before `perf_start`.
- `line_too_large`: increase `editor/ai_agent_control/max_line_bytes` or split
  the request.

## Validation

Automated coverage lives in `tests/main/test_cli_ai_input_server.h` and focuses
on parser, validation, mouse button parsing, advanced input expansion, and
runtime reuse of the CLI performance recorder.

Manual validation should cover:

- non-headless 3D project input and screenshots
- headless action/key/batch/perf behavior
- `--quit-after` coexistence
- `--perf-record` coexistence with AI runtime perf
- editor Run Bar toggle argument forwarding

## Known Limits

- v1 binds only to `127.0.0.1`.
- v1 has no token authentication.
- v1 does not expose arbitrary node properties, method calls, script execution,
  or object mutation.
- v1 has no batch cancel, pause, resume, or job query command.
- Screenshot responses return PNG file paths, not base64 payloads.

## Related

- [CLI Performance Recorder](cli-performance-recorder.md)
- [Custom Feature Branching](../operations/custom-feature-branching.md)
