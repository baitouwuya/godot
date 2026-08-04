# Deterministic GDExtension Build Flow

This is the implementation contract for the embedded GDExtension build tools. It deliberately does not expose an arbitrary shell command through MCP.

## Public tools

- `godot.gdextension.build`: validate a project-local `.gdextension`, select the fixed godot-cpp SCons profile, validate the produced library, and optionally reload or restart.
- `godot.gdextension.build_status`: return bounded progress and the latest structured diagnostics for a job owned by the calling MCP session.
- `godot.gdextension.build_cancel`: cancel a queued or running build when the selected process gateway supports cancellation.

The build input is a closed object. The extension path is a project-local `res://` path. `profile` is an enum backed by a compiled-in allowlist, `clean` is a boolean, `reload` is `none`, `if_reloadable`, or `restart_runtime`, and `timeoutMs` is bounded. A client may pass `expectedProjectRevision` to reject a build against stale project configuration.

## Ownership and state machine

`MCPGDExtensionProvider` owns registration only. `MCPGDExtensionBuildService` owns the project-scoped job state, validates arguments, launches the selected command, drains bounded output, parses diagnostics, verifies the artifact, and performs reload/restart. `MCPGDExtensionProfileRegistry` is the narrow allowlist for profile names, SCons targets, and platform selection; `MCPGDExtensionToolUtils` derives its profile enum from that registry. `MCPProjectRevision` supplies the stale-project guard shared with `godot.project.apply`.

The process gateway is the engine's asynchronous `OS::execute_with_pipe` API. It is available on the supported Unix and Windows drivers, so the editor thread is not blocked while stdout/stderr are drained. The service keeps one active build per project, caps retained jobs and output, and kills jobs at the deadline or on cancellation.

Jobs are session-owned, generation-aware, and project-scoped. A project may have one active build. Terminal states are `succeeded`, `failed`, `cancelled`, and `needs_restart`; `needs_restart` is a successful artifact update for an extension that cannot be safely reloaded in the current editor process.

## Deterministic execution

The service must validate the complete request before starting a process:

- canonicalize and constrain every path to the project root;
- parse the `.gdextension` with `ConfigFile` and resolve only the current platform library;
- reject missing, ambiguous, or outside-project libraries;
- resolve a registered profile, never a client-provided executable or shell fragment;
- capture the project revision and builder input manifest;
- enforce output, timeout, process, and diagnostic budgets.

The builder writes to its normal godot-cpp output location. Before starting, the service retains one rollback copy of an existing library. Replacement is accepted only after exit code, library existence, file size, and checksum checks pass; failures report a stable `failure.code` plus `rollbackComplete` and restore the previous library when possible.

## Reload and restart policy

After a successful replacement, `reload=if_reloadable` calls `GDExtensionManager::reload_extension` only when the extension is loaded, editor reload is enabled, and the extension configuration allows reloading. A manager result of `LOAD_STATUS_NEEDS_RESTART` becomes `needs_restart`; it is not treated as a successful reload. `restart_runtime` uses the existing `EditorRunBar` runtime generation and then runs the ClassDB/runtime probe. The service never silently restarts the editor process.

## Verification gates

Every build result includes the input revision, builder profile, artifact checksum, reload status, bounded diagnostics, and (for terminal failures) a stable failure code. The focused provider tests cover the closed schema contract; the real stdio smoke covers invalid profile/path and missing-builder responses. Architecture, build-surface, manifest, full `[MCP]*`, and Host/stdio smoke remain required before merging.

Future extraction points are intentionally narrow: a profile registry can replace the current fixed SCons argument builder, and an artifact store can add staging/architecture checks without changing the MCP tool contract.
