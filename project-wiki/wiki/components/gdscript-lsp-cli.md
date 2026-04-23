# GDScript LSP CLI

Branch-local custom feature for this Godot 4.6 repository. This page is the
single durable reference for the custom one-shot GDScript LSP CLI and is kept
outside the official `doc/` tree on purpose.

## Summary

This feature adds an editor-backed command-line path for:

- one-shot GDScript LSP queries with JSON output
- whole-project GDScript diagnostics
- fast diagnostics summaries for automation
- scriptable diagnostics exit policies

The default startup path favors low side effects over full editor fidelity.

## Quick Start

If you only need to use the feature, start here.

Run a symbol query:

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --headless `
  --path "E:\Godot Projects\view3d\project" `
  --lsp-query document-symbol `
  --file main.gd
```

Run whole-project diagnostics and always return success after execution:

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --headless `
  --path "E:\Godot Projects\view3d\project" `
  --lsp-diagnostics `
  --diagnostics-format summary `
  --diagnostics-fail-on never
```

Fail when warnings or errors exist:

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --headless `
  --path "E:\Godot Projects\view3d\project" `
  --lsp-diagnostics `
  --diagnostics-format summary `
  --diagnostics-fail-on warning
```

## Common Tasks

Use these patterns most often:

- quick health check
  `--lsp-diagnostics --diagnostics-format summary --diagnostics-fail-on never`
- CI or agent gate
  `--lsp-diagnostics --diagnostics-format summary --diagnostics-fail-on warning`
- project-relative file query
  `--lsp-query document-symbol --file scripts/player.gd`
- full custom request body
  `--lsp-query references --params-json "<json>"`

## CLI Surface

### Entry points

- `--lsp-query <operation>`
- `--lsp-diagnostics`

### Query operations

- `hover`
- `definition`
- `declaration`
- `references`
- `document-symbol`
- `completion`
- `signature-help`

### Query arguments

- `--file <path>`
  Accepts `res://`, `file://`, or project-relative paths such as `main.gd`.
- `--line <line>`
  One-based line for position-based operations.
- `--column <column>`
  One-based column for position-based operations.
- `--params-json <json>`
  Full LSP params object.
- `--include-declaration <bool>`
  Valid for `references` only.

### Diagnostics arguments

- `--diagnostics-format <jsonl|json|summary>`
- `--diagnostics-severity <error|warning|all>`
- `--diagnostics-fail-on <error|warning|any|never>`

### Diagnostics summary shape

`summary` emits one JSON object with:

- `total`
- `bySeverity`
- `byFile`

Example:

```json
{
  "total": 65,
  "bySeverity": {
    "warning": 65
  },
  "byFile": {
    "res://main.gd": 17
  }
}
```

### Exit behavior

- query success: exit `0`
- diagnostics success without fail condition: exit `0`
- diagnostics success with fail condition met: exit `1`
- invalid CLI arguments: exit `1`

## Design Notes

Read this section only if you need to maintain or extend the feature.

### Why the startup path is minimal

The CLI defaults to a minimal editor startup built on recovery-mode semantics.
That reduces project editor plugin side effects during automation runs.

### Why the implementation reuses existing code

The feature intentionally reuses the existing GDScript LSP stack instead of
building a parallel CLI-only implementation.

Primary runtime path:

- [main/main.cpp](../../../main/main.cpp)
- [modules/gdscript/language_server/gdscript_lsp_cli_runner.h](../../../modules/gdscript/language_server/gdscript_lsp_cli_runner.h)
- [modules/gdscript/language_server/gdscript_lsp_cli_runner.cpp](../../../modules/gdscript/language_server/gdscript_lsp_cli_runner.cpp)
- [modules/gdscript/language_server/gdscript_workspace.cpp](../../../modules/gdscript/language_server/gdscript_workspace.cpp)
- [editor/file_system/editor_file_system.cpp](../../../editor/file_system/editor_file_system.cpp)

### Main behavior changes

- CLI-specific argument parsing moved out of `main` and into the runner
- project-relative `--file` values normalize to `res://...`
- `references` requests can inject `includeDeclaration`
- diagnostics can emit a summary object
- diagnostics exit codes are controlled by `--diagnostics-fail-on`
- first-scan plugin initialization is skipped under recovery-mode startup

## Validation

Read this section when you need confidence, not just usage.

### Build and unit checks

Validated on this branch with:

```powershell
scons platform=windows target=editor dev_build=yes module_mono_enabled=no tests=yes d3d12=no -j2
```

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --test `
  --test-suite="[Modules][GDScript][LSP][Editor]" `
  --test-case="*[cli_runner]*"
```

Observed result:

- `3 passed`
- `0 failed`

### Real-project smoke tests

Validated sequentially against:

- `E:\Godot Projects\view3d\project`
- `E:\Godot Projects\rush-pet`
- `E:\Godot Projects\mysterious-museum`

Observed stable outcomes:

- diagnostics summary works
- diagnostics fail-on exit behavior works
- project-relative file queries work
- output stays machine-friendly on these projects

## Known Limit

`E:\Godot Projects\PluginTest` still exposes an editor-side edge case.

Current observed behavior:

- diagnostics summary JSON may still be printed
- query JSON may still be printed
- stderr can still include `Parse Error: Busy` from
  `res://addons/tripo-godot/editor/*.tscn`
- process exit code can still become `1` because of those editor resource
  loading failures

This means the minimal startup path is good enough for several real projects,
but it does not yet isolate every plugin-heavy editor resource path.

## Next Improvement Targets

- trace which editor-side resource load path still reaches
  `addons/tripo-godot/editor/*.tscn` in `PluginTest`
- keep the current minimal startup as the default path
- add any future full-editor mode only as an explicit opt-in

## Related

- [../index.md](../index.md)
- [../log.md](../log.md)
