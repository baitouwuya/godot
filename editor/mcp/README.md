# Godot MCP Host and CLI

Godot exposes MCP as an opt-in editor Host plus two terminal CLI commands. A normal editor or game launch does not start MCP.

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

The bridge reads one JSON-RPC object or batch per stdin line. It resolves the selected project's current discovery record, adds the private bearer credential internally, forwards requests to that Host, and writes only JSON-RPC responses to stdout. Blank input lines are ignored and diagnostics use stderr.

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

## Edit Scene Structure

Node structure changes use the current scene's `EditorUndoRedoManager` history and leave the scene unsaved until `godot.scene.save` is called. The structural tools are:

- `godot.node.delete`
- `godot.node.rename`
- `godot.node.reparent`
- `godot.node.move`
- `godot.node.duplicate`
- `godot.node.instantiate_scene`

Only the edited scene root and nodes owned by it are editable. Operations reject foreign, inherited, and internal nodes, prevent root deletion or reparenting, and reject parent cycles. Rename, reparent, and delete reuse the Scene dock's path rewrite machinery so exported `NodePath` properties, resource properties, and animation paths stay aligned with the edited tree. Duplication uses the editor duplication path, and scene instantiation accepts only a project-local `PackedScene` while rejecting cyclic scene dependencies.

## Find Script Usages

`godot.script.usages` accepts the same external-script `path` or edited-scene `nodePath` selector as `godot.script.get`, plus an exact `member` selector. Supported member kinds are classes, properties, constants, enums, enum values, signals, methods, and parameters. Parameter selectors use `owner` for the method or signal name and may use `classPath` for a nested class.

The tool synchronizes all open GDScript editor buffers into the calling MCP analysis session, resolves the selected member through the built-in GDScript parser, and returns semantic LSP locations using zero-based UTF-16 positions. Declarations are excluded by default; set `includeDeclaration` to `true` to include them. Built-in scripts and unsaved buffers participate in the search without being written to disk.

## Smoke Test

The end-to-end PowerShell smoke test requires PowerShell 7.2 or newer and an editor binary built with MCP support:

```powershell
pwsh -File tests/editor/mcp/test_mcp_cli_smoke.ps1 `
  -Binary bin/godot.windows.editor.dev.x86_64.console.exe
```

Optional parameters are `-TimeoutSeconds <5-300>` and `-KeepTemporaryProjects`. The test creates two temporary projects and verifies explicit-path errors, CLI/editor conflicts, ordinary editor behavior, public discovery fields, independent multi-project routing, same-project Host exclusion, JSON-only stdio stdout, the complete 35-tool MCP surface, native class search/documentation, cross-session dirty ScriptEditor revision/diagnostic/save/usage behavior, structural Node edits with `NodePath` rewrites, and explicit scene save. Temporary editor plugins request clean Host shutdown; forced termination is used only as a timeout fallback.
