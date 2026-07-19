# Godot MCP Host and CLI Transport Adapters

Godot exposes MCP as an opt-in editor Host plus two terminal CLI transport adapters. The editor Host, MCP protocol, Tool Registry, and Providers are the only source of business functionality. The CLI implements only project discovery and standard stdio forwarding; it has no business Tool Registry and never invokes a Provider directly. A normal editor or game launch does not start MCP.

Every advertised tool includes an object `outputSchema` and the standard MCP `readOnlyHint`, `destructiveHint`, `idempotentHint`, and `openWorldHint` annotations. Godot tools always declare `openWorldHint=false`; Providers classify state-changing and potentially destructive operations explicitly through the shared tool-definition factory.

## Start a Host

Start one editor Host for a project with an explicit project path:

```powershell
godot --editor --path C:\projects\my-game --mcp
```

`--mcp` is valid only with `--editor` and an explicit `--path`. The Host binds to loopback and chooses an available port by default. Use `--mcp-port <port>` to request a fixed port; `--mcp-port 0` explicitly requests an ephemeral port.

Only one MCP Host may own a canonical project path at a time. A second headless Host exits with an error. A graphical editor shows a conflict dialog that can retry, continue without MCP, or exit. Different projects may run Hosts at the same time.

Stopping the owning editor stops the Host and removes its discovery record. MCP startup is never implied by `--editor`, `--path`, or ordinary project execution.

## Discover a Project Host

Use the project path to resolve the currently owning Host:

```powershell
godot --mcp-discover --path C:\projects\my-game
```

Successful stdout is exactly one JSON object:

```json
{"projectId":"...","instanceId":"...","pid":1234,"endpoint":"http://127.0.0.1:32145/mcp","protocolVersion":"2025-11-25"}
```

Discovery deliberately omits `authSecret` and the canonical project path. Diagnostics go to stderr. The command is terminal, forces headless drivers, cannot be combined with editor or project manager mode, and fails without an explicit `--path`.

## Use the stdio Bridge

The stdio bridge lets a standard MCP client reach the project-specific Streamable HTTP Host without handling discovery records or bearer credentials:

```powershell
godot --mcp-stdio --path C:\projects\my-game
```

The bridge reads one JSON-RPC message per stdin line. Legacy `2025-03-26` sessions may use JSON-RPC batches; newer MCP protocol revisions do not support JSON-RPC batching. Use `godot.automation.batch` when an ordered tool batch is needed. The bridge resolves the selected project's current discovery record, adds the private bearer credential internally, forwards requests to that Host, and writes only JSON-RPC responses to stdout. Blank input lines are ignored and diagnostics use stderr.

`--mcp-discover` and `--mcp-stdio` are the complete CLI adapter surface. Features such as logging, LSP queries, runtime input, performance recording, and Harness jobs are MCP tools served by the editor Host, including when a client reaches them through stdio.

An MCP client configuration should keep the executable and arguments separate so paths with spaces remain intact:

```json
{
  "command": "C:\\tools\\godot.exe",
  "args": ["--mcp-stdio", "--path", "C:\\projects\\my-game"]
}
```

Start the editor Host before starting the bridge. Each bridge is tied to the project named by its own `--path`; it does not route through whichever Godot editor happened to start first.

## Multiple Projects

Project routing is path-based:

1. Godot canonicalizes the explicit project directory and derives a stable project ID.
2. Each running Host publishes an instance-specific endpoint under that project ID.
3. `--mcp-discover` and `--mcp-stdio` resolve only the record matching their explicit project path.
4. The per-project lease prevents a second Host from replacing a live owner, while unrelated project IDs remain independent.

Do not cache one endpoint globally or reuse one project's bridge configuration for another project. Create one MCP client entry per project path. Restarting an editor creates a new instance ID, endpoint, and bearer credential, so clients should rediscover through the CLI.

## Search Class Documentation

`godot.class.search` searches the editor's merged class documentation index, including native engine classes, extension classes, built-in Variant types, and documented project script classes. It supports fuzzy class-name matching, native/script source filtering, inheritance filtering, deprecated-class inclusion, and bounded result counts.

`godot.class.get_documentation` reads one exact class from the same index. The default `overview` section returns class-level documentation, tutorials, inheritance, and grouped member counts. Select `all`, `constructors`, `methods`, `operators`, `signals`, `properties`, `constants`, `enums`, `themeItems`, or `annotations` to retrieve compact structured members. Add an exact `member` name to isolate one member, and set `view` to `full` to include member descriptions. Documentation text uses the same Markdown conversion as the built-in GDScript LSP.

## Inspect Node Properties

`godot.node.get_properties` returns every property marked for editor use in the edited node's Inspector property list, including properties exported by attached and inherited scripts. The `properties` array preserves the Inspector order and identifies each entry as `native` or `script`; script entries include their owning script path when one is available. Each entry also reports its Variant type, hint, usage flags, read-only state, and whether its current value was available and safely encodable.

The result includes `propertyCount`, `scriptPropertyCount`, and a `propertyLayout` array containing the original category, group, subgroup, and property sequence. Property layout entries refer to values in `properties` through `propertyIndex`.

Unsafe object-like values remain listed with their complete property metadata but omit `value`. A non-null `Node` value additionally exposes a read-only `valueReference` descriptor, using an edited-scene-relative path when possible; this descriptor is not accepted by `godot.node.set_property`. Null object values use JSON `null`. Project `Resource` references are encoded as restricted `res://` references; other arbitrary `Object`, `Callable`, `Signal`, and `RID` values are not exposed.

## Operate The Editor UI

`godot.editor.ui.get_actions` returns a bounded snapshot of the currently visible editor UI. Each actionable control or logical child item includes an opaque `targetId`, role, label, class, bounds, state, and the semantic actions it accepts. Buttons, menus, option lists, text fields, numeric ranges, item lists, tabs, and tree navigation are supported. Secret `LineEdit` values are never returned. Tree values remain read-only through this generic UI layer because editor trees such as the Inspector and Scene dock require their own undo-aware domain operations.

Pass the returned `snapshotId`, one `targetId`, and an advertised action to `godot.editor.ui.perform`. Supported actions are `focus`, `click`, `activate`, `select`, `set_value`, `expand`, `collapse`, `increment`, and `decrement`. Button clicks use `BaseButton`'s native semantic activation path; structured controls reuse their native selection and value APIs. A successful operation invalidates the snapshot, so inspect the current page again before performing another operation.

Snapshots are isolated per MCP session. Targets are revalidated against the visible editor tree and active window before every operation. When a modal dialog or popup is active, only that scope is exposed. Raw object IDs, node paths, arbitrary callables, and screen-coordinate clicks are not accepted.

## Batch MCP Tools

`godot.automation.batch` prevalidates and then invokes up to 64 MCP tools sequentially in the same session. It is fail-fast by default and rejects recursive batch calls. CLI clients obtain this capability only through the standard stdio bridge to the project's running MCP Host; there is no separate batch CLI implementation. Long-running runtime automation belongs in the asynchronous Harness instead of one synchronous batch.

## Edit Scene Structure

`godot.scene.open` opens a project-local `.tscn` or `.scn` through the editor. Node structure changes use the current scene's `EditorUndoRedoManager` history and leave the scene unsaved until `godot.scene.save` is called. The structural tools are:

- `godot.node.delete`
- `godot.node.rename`
- `godot.node.reparent`
- `godot.node.move`
- `godot.node.duplicate`
- `godot.node.instantiate_scene`

Only the edited scene root and nodes owned by it are editable. Operations reject foreign, inherited, and internal nodes, prevent root deletion or reparenting, and reject parent cycles. Rename, reparent, and delete reuse the Scene dock's path rewrite machinery so exported `NodePath` properties, resource properties, and animation paths stay aligned with the edited tree. Duplication uses the editor duplication path, and scene instantiation accepts only a project-local `PackedScene` while rejecting cyclic scene dependencies.

## Edit Node Groups and Signals

`godot.node.get_groups` lists a scene node's groups and whether each one is persisted. `godot.node.add_to_group` and `godot.node.remove_from_group` change those groups through the same editor Undo/Redo history as other scene edits; new groups are persistent by default.

`godot.node.get_signal_connections` lists only persistent connections whose source and target are editable nodes in the active scene. `godot.node.connect_signal` creates a persistent connection, optionally with deferred or one-shot behavior, and `godot.node.disconnect_signal` removes one. Both endpoints and the target method are validated before an undoable change is made. Runtime-only, inherited, external, custom-callable, and non-persistent connections are deliberately excluded so MCP cannot silently alter connections that the edited scene does not own.

## Find Script Usages

`godot.script.open` accepts the same external-script `path` or edited-scene `nodePath` selector as the other script tools. It opens the authoritative Script editor buffer without returning source text; built-in GDScripts are supported through their scene subresource or attached node.

`godot.script.usages` accepts the same external-script `path` or edited-scene `nodePath` selector as `godot.script.get`, plus an exact `member` selector. Supported member kinds are classes, properties, constants, enums, enum values, signals, methods, and parameters. Parameter selectors use `owner` for the method or signal name and may use `classPath` for a nested class.

The tool synchronizes all open GDScript editor buffers into the calling MCP analysis session, resolves the selected member through the built-in GDScript parser, and returns semantic LSP locations using zero-based UTF-16 positions. Declarations are excluded by default; set `includeDeclaration` to `true` to include them. Built-in scripts and unsaved buffers participate in the search without being written to disk.

## Edit Resources

`godot.resource.get_properties` reads the editable Inspector property list of a project-local Resource from `ResourceLoader`'s cache. Values use the same restricted Variant codec as node properties. `godot.resource.set_property` updates that cached object, integrates with editor Undo/Redo when available, refreshes the Inspector, and deliberately leaves the resource unsaved. `godot.resource.save` explicitly persists the same cached object. Arbitrary paths and unsafe object references are rejected.

`godot.resource.import` rolls back the target, metadata, previous destination files, and files in the importer-owned save namespace. A custom importer that reports a new destination outside those known namespaces is never allowed to make MCP delete a path whose prior ownership cannot be proven; failed imports report such paths in `possibleResidualProducts` with `rollbackComplete: false`.

## Inspect Debug Output

`godot.debug.get_logs` reads a bounded snapshot of editor and running-project output. It can filter by source, severity, debugger session, runtime generation, sequence, and case-insensitive text. Deduplication is enabled by default and groups identical events globally, including non-adjacent repeats, while preserving occurrence counts and first/last sequence numbers. Set `deduplicate` to `false` for individual events or `includeStack` to include retained representative frames.

`godot.debug.get_latest_log` reads only the current project's configured log and its rotated siblings. It returns either a bounded raw tail or a compact summary with normalized changing values, folded repeats, issue counts, representative stacks, top templates, and keywords. It does not accept arbitrary file paths.

`godot.debug.get_errors` returns the same compact structure restricted to errors and, by default, warnings. Each retained representative event has an `errorId`; pass it to `godot.debug.get_stack` to retrieve its runtime stack. Alternatively, pass a `debuggerSession` and optional `threadId` to inspect a running project's paused stack. Runtime generations prevent reused debugger tabs from merging separate runs. Editor errors do not claim a stack when Godot's local error handler did not provide one. Debugger locations and stack lines retain Godot's native one-based line numbering.

Captured history uses a bounded in-memory store. Query responses expose the store generation, next sequence, and dropped-event count so clients can page incrementally with `sinceSequence` and detect overwritten history.

## Control A Running Project

Runtime tools reuse the editor's built-in run bar and remote debugger connection. They do not open a second control socket inside the game:

- `godot.runtime.get_state` lists the editor run state and active debugger sessions.
- `godot.runtime.play` starts the main scene, current edited scene, or an explicit project `PackedScene`; `godot.runtime.stop` stops the editor-launched project.
- `godot.runtime.pause`, `resume`, and `next_frame` use the existing SceneTree suspension protocol.
- `godot.runtime.debug.break`, `continue`, `step_into`, `step_over`, and `step_out` use the built-in script debugger. They require the target `runtimeGeneration`; step operations require a debuggable paused breakpoint.
- `godot.runtime.get_tree` returns a fresh remote scene tree. Runtime `objectId` values are decimal strings so JSON clients do not lose 64-bit precision.
- `godot.runtime.node.get_properties` returns the remote Inspector property list, including script members, constants, and exported properties.
- `godot.runtime.node.set_property` uses the remote Inspector setter and reads the object back to return the effective value.
- `godot.runtime.input.send` injects a bounded ordered batch of project actions, key events, mouse buttons or motion, joypad buttons or axes, and pan or magnify gestures.
- `godot.runtime.input.sequence` schedules non-blocking input operations with `waitMs`, `waitFrames`, `tap`, and `hold` steps. It returns a `sequenceId` for `sequence_status` and `sequence_cancel`.
- `godot.runtime.input.release_all` cancels sequences owned by the calling MCP session and releases all runtime inputs held by that session.
- `godot.runtime.click_target`, `double_click_target`, `hover_target`, and `focus_target` resolve a selector in the running project before injecting bounded pointer or focus operations.
- `godot.runtime.drag_target_to_target` resolves both endpoints atomically and runs the drag through the existing input sequence scheduler.
- `godot.runtime.type_text` focuses a runtime Control and injects bounded Unicode key events; `godot.runtime.scroll_view` injects bounded wheel input at a selector-resolved target.
- `godot.runtime.wait.start` creates an asynchronous `node_exists`, `node_gone`, `property`, `scene_changed`, or `screenshot_diff` condition job in the running project. `wait.status` polls it and `wait.cancel` stops it without holding the editor main thread for the condition duration.
- `godot.runtime.performance.start` creates a bounded in-process sampler that shares one monitor collection pass per runtime frame across active MCP jobs. `performance.status` returns lightweight progress, while `performance.stop` returns the final compressed summary.
- `godot.runtime.harness.start` compiles and starts a bounded asynchronous automation plan. `harness.status`, `harness.cancel`, and `harness.get_report` expose progress and evidence without blocking the editor while input sequences or runtime waits are active.

When more than one project instance is connected to the same editor, runtime tools require the `debuggerSession` returned by `godot.runtime.get_state`. Mutating tools also require `runtimeGeneration`; a generation changes whenever that debugger slot connects to a new process, so delayed calls cannot accidentally modify a restarted game. Scene tree and property requests use a bounded remote-debugger round trip and return `RUNTIME_TIMEOUT` if the game does not answer.

Input objects use a semantic JSON shape and are validated in the editor before Godot's existing `input_event_codec` serializes them. The required `type` is one of `action`, `key`, `mouse_button`, `mouse_motion`, `joypad_button`, `joypad_motion`, `pan`, or `magnify`. Stateful action, key, mouse button, and joypad button events require an explicit `pressed`; joypad motion requires an explicit `axisValue`. Pointer positions and deltas use two-number arrays such as `[320, 180]`. The running project decodes each event and passes it to `Input::parse_input_event()` on its main thread.

Each sequence step specifies exactly one operation. An `event` contains a normal input object. `waitMs` and `waitFrames` are positive integers. `tap` and `hold` contain a binary stateful input object without `pressed`; `tap` defaults to 50 milliseconds, while `hold` requires exactly one `durationMs` or `durationFrames`. The editor validates and encodes the complete sequence, then sends it atomically to the running project. Millisecond waits use the runtime monotonic clock and frame waits advance from the runtime `SceneTree.process_frame` signal, so they do not depend on the editor frame rate. A sequence contains at most 256 source steps, 768 compiled steps, ten minutes of millisecond waits, and ten minutes at 60 runtime frames per second. Encoded runtime events are limited to 256 bytes each, and all queued sequences share a one MiB encoded-input budget. Up to 32 sequences may be active at once, with a global runtime budget of 128 injected events per frame.

Held inputs are tracked inside the running project by MCP session and sequence owner. Multiple clients may hold the same input; Godot sends the release only after the final owner releases it. Joypad axes additionally restore the most recent remaining owner's value. Completing or cancelling a sequence releases inputs owned by that sequence. Deleting or expiring an MCP session, stopping the project, restarting the runtime, or shutting down the MCP Host also clears its tracked state. The editor sends a one-second heartbeat while MCP is active; the running project releases all MCP-owned input after five seconds without a heartbeat, covering editor crashes and debugger disconnects. Use `release_all` as an explicit safety operation after interrupted automation.

```json
{
  "runtimeGeneration": 4,
  "steps": [
    { "hold": { "type": "action", "action": "move_right" }, "durationFrames": 30 },
    { "waitMs": 100 },
    { "tap": { "type": "key", "keycode": 32 }, "durationMs": 60 }
  ]
}
```

## Smoke Test

The end-to-end PowerShell smoke test requires PowerShell 7.2 or newer and an editor binary built with MCP support:

```powershell
pwsh -File tests/editor/mcp/test_mcp_cli_smoke.ps1 `
  -Binary bin/godot.windows.editor.dev.x86_64.console.exe
```

Optional parameters are `-TimeoutSeconds <5-300>` and `-KeepTemporaryProjects`. The test creates two temporary projects and verifies explicit-path errors, CLI/editor conflicts, ordinary editor behavior, public discovery fields, independent multi-project routing, same-project Host exclusion, JSON-only stdio stdout, the complete 94-tool MCP surface, native class search/documentation, editor UI discovery, runtime tool discovery, compressed debug output and errors, cross-session dirty ScriptEditor revision/diagnostic/save/usage behavior, structural Node edits with `NodePath` rewrites, persistent node group and signal changes, and explicit scene save. Temporary editor plugins request clean Host shutdown; forced termination is used only as a timeout fallback.
