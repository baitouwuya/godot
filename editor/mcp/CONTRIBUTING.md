# Contributing Embedded MCP Features

Read `ARCHITECTURE.md` before changing a dependency or lifecycle boundary. Keep the public protocol stable unless the change explicitly updates the compatibility surface.

## Add Or Change A Tool

1. Find the existing Provider and domain service that own the behavior. Extend them instead of adding a parallel implementation.
2. Put input and output schemas in the domain's `*_tool_utils` or `*_tool_schemas` module. Use shared schema builders and close input objects with `additionalProperties: false`.
3. Keep the Provider method limited to MCP argument/result adaptation. Put editor state, transactions, runtime requests, parsing, and retained state in focused collaborators.
4. Register through `MCPToolUtils::register_tools`. Set annotations from actual side effects; every Godot tool uses `openWorldHint=false`.
5. Return results through `MCPToolUtils::make_success_result` or `make_error_result`. Never create a second JSON-RPC or MCP response envelope in a Provider.
6. Add focused C++ tests for schema, argument errors, successful output, lifecycle cleanup, and domain behavior. Mutating tools need Undo/Redo or cleanup coverage. Async tools need bounded polling, cancellation, session removal, and shutdown coverage.
7. If the public tool set changes, update the ordered `tests/editor/mcp/data/mcp_tool_manifest.json` snapshot and the expected count intentionally. Do not generate Provider registration from the snapshot.
8. Extend the real Python smoke when unit tests cannot prove cross-process discovery, transport, editor, or runtime behavior.

## Add A Feature

Use `MCPEditorFeature` only when an independently owned capability needs lifecycle callbacks. Add the concrete feature to `MCPBuiltinFeatures`, pass shared dependencies explicitly, and preserve reverse-order cleanup. A collection of helper functions does not need a feature object.

A new feature must define:

- who allocates and deletes it;
- who owns each retained state collection;
- how registration failure rolls back;
- what session removal releases;
- what shutdown cancels or clears;
- whether it requires Editor MCP, Runtime MCP, GDScript LSP, or another optional build surface.

## Split A Service

Split by ownership and side effect, not by file length. Preserve a stable Facade when callers already depend on the service. Typical collaborators are Store, Builder, Validator, Executor, Compiler, and Gateway. A collaborator should have one reason to change and no hidden lookup of another service.

Avoid abstractions that merely rename calls. Extract when the new component owns state, isolates a side effect, makes an invariant testable, or removes repeated domain logic.

## Required Checks

Run the static boundary, build-surface, and manifest validators before compiling. Build the narrowest affected surface, run focused tests, then run the complete `[MCP]*` suite. Any Host, transport, lifecycle, tool-surface, editor transaction, or runtime change also requires the Python CLI smoke test.

Use separate worktrees or serialized builds for different SCU configurations. Preserve unrelated worktree changes and commit each architecture concern separately.
