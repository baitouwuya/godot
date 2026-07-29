# Embedded MCP Architecture

## Decision

Godot MCP remains an opt-in embedded development feature. It is not an editor addon, GDExtension, external plugin, or separate service. Embedding is intentional because the tools need authoritative access to editor state, Undo/Redo, ScriptEditor buffers, the debugger, the run bar, and runtime debugger messages. Reimplementing those integrations across a plugin boundary would add synchronization and ownership problems without adding a useful deployment boundary.

The feature is excluded from release templates. Editor MCP and runtime MCP are separate build surfaces so a debug template can include only the runtime debugger support it needs.

## Dependency Direction

Dependencies flow down this list and must not flow back up:

```text
main/mcp_* CLI adapters
        |
editor/mcp Host and transports
        |
core/mcp protocol and Tool Registry
        |
editor/mcp feature lifecycle and built-in composition
        |
editor/mcp/providers Provider adapters
        |
services, stores, builders, executors, and gateways
        |
Godot editor and runtime domain APIs
```

- CLI adapters discover a project Host or forward stdio. They contain no tools and invoke no Provider directly.
- Host and transport code depend on the Tool Registry, never on a concrete Provider.
- `core/mcp` has no editor, main, or scene dependency.
- Providers do not include other Provider headers. Shared behavior belongs in a service or focused utility.
- `scene/debugger/mcp_runtime_*` code has no editor dependency. It communicates through the existing debugger protocol.

`misc/scripts/validate_mcp_dependencies.py` enforces these boundaries. `misc/scripts/validate_mcp_build_surfaces.py` enforces the supported build surfaces.

## Ownership And Lifecycle

`MCPBuiltinFeatures` is the composition root. It creates built-in Providers and shared services, wires explicit dependencies, establishes registration order, and destroys objects in reverse dependency order. It must not absorb Provider business logic.

`MCPEditorFeatureSet` is a non-owning lifecycle coordinator. A feature may register and unregister tools, process bounded work, release session-owned state, and shut down. Registration rollback, session removal, and shutdown must remain idempotent.

State has one owner:

- A store owns retained jobs, snapshots, revisions, events, or session mappings.
- An executor owns side effects and state transitions for one domain.
- A builder creates stable protocol output without owning mutable domain state.
- A gateway owns one editor/runtime integration boundary and its deadlines.
- A service Facade validates domain preconditions and composes those collaborators.
- A Provider maps MCP arguments and results to the service and owns tool registration only.

Do not introduce a Service Locator, universal context object, generic transport framework, or a second Tool Registry. Dependencies are passed explicitly through constructors or narrow setters at the composition root.

## Protocol Contract

The Tool Registry is the runtime source of truth for definitions and handlers. Every advertised tool has:

- a closed object `inputSchema`;
- an object `outputSchema`;
- `readOnlyHint`, `destructiveHint`, `idempotentHint`, and `openWorldHint`;
- output validation for successful `structuredContent`;
- session and generation checks when it owns live editor or runtime state.

`tests/editor/mcp/data/mcp_tool_manifest.json` is an ordered compatibility snapshot, not generated registration code. The static manifest validator checks its shape. The cross-platform smoke test compares it with `tools/list` from a real running Host; that comparison detects missing, added, or reordered tools.

JSON integers remain integers. Values that can exceed interoperable JSON integer precision, such as runtime object IDs, use decimal strings. Godot-only Variant values use the explicit MCP Variant codec rather than private string prefixes.

Successful modern-protocol responses keep `structuredContent` authoritative. Small responses also mirror JSON in `content` for older clients; large responses use a bounded summary to avoid doubling payload size.

## Concurrency And Safety

Editor mutations run on the editor main thread and use the existing domain APIs. Runtime requests use bounded debugger round trips; asynchronous waits, input sequences, performance sampling, and Harness jobs advance incrementally without blocking the editor.

Every retained operation is bounded by counts, bytes, deadlines, or both. Session removal, runtime restart, Host shutdown, and crash heartbeats release owned state. Mutations use current debugger session and runtime generation tokens so delayed calls cannot target a restarted project.

## Verification

Architecture changes are complete only when the relevant layers pass:

```bash
python3 misc/scripts/validate_mcp_dependencies.py --root .
python3 misc/scripts/validate_mcp_build_surfaces.py --root .
python3 misc/scripts/validate_mcp_tool_manifest.py --root .
python3 misc/scripts/test_mcp_scons_config.py
python3 -m unittest \
  tests.misc.test_validate_mcp_dependencies \
  tests.misc.test_validate_mcp_build_surfaces \
  tests.misc.test_validate_mcp_tool_manifest
./bin/godot.macos.editor.dev.arm64 --headless --test --test-case='[MCP]*'
python3 tests/editor/mcp/test_mcp_cli_smoke.py \
  --binary ./bin/godot.macos.editor.dev.arm64
```

Build checks must also cover MCP enabled and disabled, Editor and debug-template surfaces, profile/command-line precedence, and SCU enabled and disabled. Do not run different SCU configurations concurrently in one worktree because they share generated `.scu` files.

GitHub Actions runs the static architecture validators and their unit tests before platform builds. The Linux matrix explicitly compiles an MCP-enabled Editor and runs the SCons config plus real Host + stdio smoke tests, compiles an MCP-enabled SCU Editor, compiles an MCP-disabled non-SCU Editor, and compiles the MCP runtime bridge into a debug template. An existing Windows Editor job also runs the cross-platform Host + stdio smoke. The config smoke exercises profile and command-line precedence, release-template rejection, and disabled GDScript source selection without adding duplicate CI builds.
