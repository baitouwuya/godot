# Deterministic GDExtension Build Flow

This is the implementation contract for the future embedded GDExtension build tools. It deliberately does not expose an arbitrary shell command through MCP.

## Public tools

- `godot.gdextension.build`: validate a project-local `.gdextension`, select a registered builder profile, build into a staging directory, validate the produced library, and optionally reload or restart.
- `godot.gdextension.build_status`: return bounded progress and the latest structured diagnostics for a job owned by the calling MCP session.
- `godot.gdextension.build_cancel`: cancel a queued or running build when the selected process gateway supports cancellation.

The build input is a closed object. The extension path is a project-local `res://` path. `profile` is an enum backed by a compiled-in allowlist, `clean` is a boolean, `reload` is `none`, `if_reloadable`, or `restart_runtime`, and `timeoutMs` is bounded. A client may pass `expectedProjectRevision` to reject a build against stale project configuration.

## Ownership and state machine

`MCPGDExtensionProvider` owns registration only. `MCPGDExtensionBuildService` validates arguments and composes these collaborators:

1. `MCPGDExtensionProfileRegistry` maps a profile to an executable, fixed argument vector, environment allowlist, and expected artifact layout.
2. `MCPProcessGateway` owns one asynchronous child process, bounded stdout/stderr tails, deadlines, and cancellation. It must provide the same behavior on macOS, Linux, and Windows; `OS::execute_with_pipe` is not sufficient on Unix.
3. `MCPGDExtensionArtifactStore` stages output, validates the platform library selected by the `.gdextension`, atomically replaces the destination, and retains one rollback copy.
4. `MCPGDExtensionDiagnosticParser` converts compiler output into `{severity, code, message, file, line, column}` records with bounded counts and text.
5. `MCPGDExtensionRuntimeProbe` checks `GDExtensionManager` load/reload status, ClassDB registration, and a minimal runtime smoke scene.

Jobs are session-owned, generation-aware, and project-scoped. A project may have one active build. Terminal states are `succeeded`, `failed`, `cancelled`, and `needs_restart`; `needs_restart` is a successful artifact update for an extension that cannot be safely reloaded in the current editor process.

## Deterministic execution

The service must validate the complete request before starting a process:

- canonicalize and constrain every path to the project root;
- parse the `.gdextension` with `ConfigFile` and resolve only the current platform library;
- reject missing, ambiguous, or outside-project libraries;
- resolve a registered profile, never a client-provided executable or shell fragment;
- capture the project revision and builder input manifest;
- enforce output, timeout, process, and diagnostic budgets.

The artifact is written to staging first. Replacement happens only after exit code, library existence, file size, architecture, and checksum checks pass. Any failure reports `rollbackComplete` and leaves the previous library active.

## Reload and restart policy

After a successful replacement, `reload=if_reloadable` calls `GDExtensionManager::reload_extension` only when the extension is loaded, editor reload is enabled, and the extension configuration allows reloading. A manager result of `LOAD_STATUS_NEEDS_RESTART` becomes `needs_restart`; it is not treated as a successful reload. `restart_runtime` uses the existing `EditorRunBar` runtime generation and then runs the ClassDB/runtime probe. The service never silently restarts the editor process.

## Verification gates

Every build result includes the input revision, builder profile, artifact checksum, reload status, and probe report. The focused provider tests cover profile rejection, stale revision, process failure, diagnostics, staging rollback, reload refusal, and session isolation. The real stdio smoke covers a successful build with a tiny fixture extension and a failed build with structured diagnostics. Architecture, build-surface, manifest, full `[MCP]*`, and Host/stdio smoke remain required before merging.
