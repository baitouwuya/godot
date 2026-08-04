#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import sys
import tempfile
from pathlib import Path
from typing import Any, Dict, Iterable, List, Mapping, Optional, Sequence

from mcp_smoke_test_utils import (
    CommandResult,
    HostProcess,
    ProcessRunner,
    SmokeFailure,
    describe_command_result,
    invoke_stdio,
    load_tool_manifest,
    parse_discovery,
    request,
    require,
    tool_call,
    wait_until,
)


INITIAL_SCRIPT_TEXT = """## Smoke generated script.
extends Node

## Stored smoke value.
var value: int = 7

func get_value() -> int:
\treturn value
"""


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def new_smoke_project(path: Path, name: str) -> None:
    write_text(
        path / "project.godot",
        f"""; Engine configuration file.
config_version=5

[application]
config/name="{name}"
run/main_scene="res://main.tscn"

[editor_plugins]
enabled=PackedStringArray("res://addons/mcp_smoke_cleanup/plugin.cfg")
""",
    )
    write_text(
        path / "main.tscn",
        """[gd_scene load_steps=3 format=3]

[ext_resource type="Script" path="res://runtime_smoke.gd" id="1_runtime"]

[node name="Root" type="Node"]
script = ExtResource("1_runtime")

[node name="RuntimeParent" type="Node2D" parent="."]
position = Vector2(20, 10)

[node name="RuntimeChild" type="Node2D" parent="RuntimeParent"]
""",
    )
    write_text(
        path / "runtime_smoke.gd",
        """extends Node

func _ready() -> void:
\tInputMap.add_action("mcp-python-runtime-action")
\tprint("mcp-python-smoke-runtime-ready")
\tpush_warning("mcp-python-smoke-runtime-warning")
\tpush_error("mcp-python-smoke-runtime-error")
""",
    )
    write_text(
        path / "undocumented_smoke_class.gd",
        """class_name SmokeUndocumentedClass
extends Node
""",
    )
    write_text(
        path / "node_script.gd",
        """extends Node
@export var target: NodePath
""",
    )
    addon = path / "addons/mcp_smoke_cleanup"
    write_text(
        addon / "plugin.cfg",
        """[plugin]
name="MCP Smoke Cleanup"
description="Stops the temporary editor process during MCP CLI smoke cleanup."
author="Godot"
version="1.0"
script="plugin.gd"
""",
    )
    write_text(
        addon / "plugin.gd",
        '''@tool
extends EditorPlugin

const STOP_FILE := "res://.mcp-smoke-stop"
const SCENE_READY_FILE := "res://.mcp-smoke-scene-ready"

func _enter_tree() -> void:
\tset_process(true)

func _process(_delta: float) -> void:
\tif FileAccess.file_exists(STOP_FILE):
\t\tget_tree().quit()
\t\treturn
\tif EditorInterface.get_edited_scene_root() == null:
\t\tEditorInterface.open_scene_from_path("res://main.tscn")
\telif not FileAccess.file_exists(SCENE_READY_FILE):
\t\tvar ready_file := FileAccess.open(SCENE_READY_FILE, FileAccess.WRITE)
\t\tif ready_file:
\t\t\tready_file.store_string("ready")
''',
    )


def tool_result(responses: Mapping[int, Mapping[str, Any]], request_id: int, label: str) -> Dict[str, Any]:
    response = responses[request_id]
    require("result" in response, f"{label} returned a JSON-RPC error: {response}")
    result = response["result"]
    require(isinstance(result, dict), f"{label} returned a non-object result.")
    return result


def structured_result(responses: Mapping[int, Mapping[str, Any]], request_id: int, label: str) -> Dict[str, Any]:
    result = tool_result(responses, request_id, label)
    require(not bool(result.get("isError", False)), f"{label} returned a tool error: {result}")
    content = result.get("structuredContent")
    require(isinstance(content, dict), f"{label} did not return structuredContent.")
    return content


def tool_error_code(responses: Mapping[int, Mapping[str, Any]], request_id: int, label: str) -> str:
    result = tool_result(responses, request_id, label)
    require(bool(result.get("isError")), f"{label} unexpectedly succeeded: {result}")
    error = result.get("structuredContent", {}).get("error", {})
    require(isinstance(error, dict), f"{label} did not return a structured error.")
    code = error.get("code")
    require(isinstance(code, str) and code, f"{label} did not return an error code.")
    return code


def text_sha256(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def children_names(tree: Mapping[str, Any]) -> List[str]:
    root = tree.get("root", {})
    return [str(child.get("name")) for child in root.get("children", []) if isinstance(child, dict)]


def wait_for_discovery(runner: ProcessRunner, host: HostProcess, timeout: int) -> Dict[str, Any]:
    def attempt() -> Optional[Dict[str, Any]]:
        host.require_running()
        result = runner.invoke(["--verbose", "--mcp-discover", "--path", str(host.project_path)])
        if result.returncode != 0:
            return None
        return parse_discovery(result, host.label)

    return wait_until(timeout, attempt, f"{host.label} discovery")


def wait_for_file(host: HostProcess, relative_path: str, timeout: int) -> None:
    target = host.project_path / relative_path

    def attempt() -> bool:
        host.require_running()
        return target.is_file()

    wait_until(timeout, attempt, f"{host.label} file {relative_path}")


def run_smoke(binary: str, timeout: int, keep_temporary_projects: bool) -> None:
    manifest_path = Path(__file__).parent / "data/mcp_tool_manifest.json"
    expected_tools = load_tool_manifest(manifest_path)
    require(len(expected_tools) == 111, f"Tool manifest must contain 111 tools, found {len(expected_tools)}.")

    temporary_root = Path(tempfile.mkdtemp(prefix="godot-mcp-python-smoke-"))
    project_a = temporary_root / "project-a"
    project_b = temporary_root / "project-b"
    new_smoke_project(project_a, "MCP Python Smoke A")
    new_smoke_project(project_b, "MCP Python Smoke B")
    runner = ProcessRunner(ProcessRunner.resolve_binary(binary), temporary_root, timeout)
    hosts: List[HostProcess] = []
    success = False

    try:
        print("[1/9] Checking explicit project path and CLI/editor conflicts")
        missing_path = runner.invoke(["--mcp-discover"])
        require(missing_path.returncode != 0, "--mcp-discover without --path must fail.")
        require(not missing_path.stdout.strip(), "missing-path diagnostics must not use stdout.")
        require("--path" in missing_path.stderr and "explicit" in missing_path.stderr.lower(), "missing-path diagnostic is not actionable.")

        mode_conflict = runner.invoke(["--editor", "--mcp-discover", "--path", str(project_a)])
        require(mode_conflict.returncode != 0, "CLI commands must conflict with editor mode.")
        require("cannot be combined" in mode_conflict.stderr.lower(), "CLI/editor conflict diagnostic is missing.")

        print("[2/9] Checking ordinary editor startup remains MCP-free")
        ordinary_process = runner.start_editor(project_a, "ordinary-editor")
        hosts.append(ordinary_process)
        wait_for_file(ordinary_process, ".mcp-smoke-scene-ready", timeout)
        ordinary = ordinary_process.stop(require_graceful=True)
        require(
            ordinary.returncode == 0,
            f"ordinary headless editor startup failed: {describe_command_result(ordinary)}",
        )
        require("MCP Host started" not in f"{ordinary.stdout}\n{ordinary.stderr}", "ordinary editor startup enabled MCP.")
        ordinary_discovery = runner.invoke(["--verbose", "--mcp-discover", "--path", str(project_a)])
        require(ordinary_discovery.returncode != 0, "ordinary editor startup unexpectedly published MCP discovery.")
        (project_a / ".mcp-smoke-scene-ready").unlink(missing_ok=True)

        print("[3/9] Starting project A Host and validating public discovery")
        host_a = runner.start_host(project_a, "project-a")
        hosts.append(host_a)
        discovery_a = wait_for_discovery(runner, host_a, timeout)

        print("[4/9] Starting project B Host and validating project routing")
        host_b = runner.start_host(project_b, "project-b")
        hosts.append(host_b)
        discovery_b = wait_for_discovery(runner, host_b, timeout)
        require(discovery_a["pid"] != discovery_b["pid"], "two simultaneous Hosts published the same PID.")
        require(discovery_a["projectId"] != discovery_b["projectId"], "two project paths resolved to the same project ID.")
        require(discovery_a["instanceId"] != discovery_b["instanceId"], "two Hosts reused an instance ID.")
        require(discovery_a["endpoint"] != discovery_b["endpoint"], "two simultaneous Hosts published the same endpoint.")

        print("[5/9] Checking same-project Host conflict")
        conflict = runner.invoke(
            ["--editor", "--headless", "--path", str(project_a), "--mcp", "--mcp-port", "0"],
            executable=runner.host_binary,
        )
        require(conflict.returncode != 0, "a second Host for project A must fail.")
        require(
            "another mcp host already owns this project" in f"{conflict.stdout}\n{conflict.stderr}".lower()
            or "project lease is owned" in f"{conflict.stdout}\n{conflict.stderr}".lower(),
            "same-project Host conflict diagnostic is missing.",
        )
        host_a.require_running()
        discovery_after_conflict = wait_for_discovery(runner, host_a, timeout)
        require(discovery_after_conflict["instanceId"] == discovery_a["instanceId"], "same-project conflict replaced the owner record.")

        print("[6/9] Checking stdio JSON, 111-tool manifest, settings, classes, and scripts")
        surface_requests = [
            request(2, "tools/list", {}),
            tool_call(3, "godot.script.create", {"path": "res://mcp_smoke_created.gd", "text": INITIAL_SCRIPT_TEXT}),
            tool_call(4, "godot.script.get", {"path": "res://mcp_smoke_created.gd"}),
            tool_call(5, "godot.project.get_setting", {"name": "application/config/name"}),
            tool_call(6, "godot.class.search", {"query": "SmokeUndocumentedClass", "source": "script", "limit": 5}),
            tool_call(7, "godot.class.get_documentation", {"name": "SmokeUndocumentedClass"}),
            tool_call(8, "godot.gdextension.build", {"extensionPath": "res://missing.gdextension", "profile": "invalid"}),
            tool_call(9, "godot.gdextension.build", {"extensionPath": "res://missing.gdextension"}),
            tool_call(10, "godot.gdextension.build", {"extensionPath": "res://smoke.gdextension"}),
        ]
        write_text(
            project_a / "smoke.gdextension",
            """[configuration]
entry_symbol = "smoke_init"
compatibility_minimum = 4.1

[libraries]
macos.debug = "res://libsmoke.dylib"
""",
        )
        surface = invoke_stdio(runner, project_a, discovery_a["protocolVersion"], surface_requests, "stdio surface")
        tools_list = surface[2]["result"]["tools"]
        require(isinstance(tools_list, list), "tools/list did not return a tools array.")
        actual_tools = [tool.get("name") for tool in tools_list]
        first_mismatch = next(
            (index for index, pair in enumerate(zip(actual_tools, expected_tools)) if pair[0] != pair[1]),
            min(len(actual_tools), len(expected_tools)),
        )
        require(
            actual_tools == expected_tools,
            "tools/list did not match the ordered 111-tool manifest: "
            f"actualCount={len(actual_tools)}, expectedCount={len(expected_tools)}, "
            f"firstMismatch={first_mismatch}, "
            f"actual={actual_tools[first_mismatch:first_mismatch + 3]!r}, "
            f"expected={expected_tools[first_mismatch:first_mismatch + 3]!r}, "
            f"missing={sorted(set(expected_tools) - set(actual_tools))!r}, "
            f"unexpected={sorted(set(actual_tools) - set(expected_tools))!r}",
        )
        for tool in tools_list:
            require(isinstance(tool.get("outputSchema"), dict), f"Tool {tool.get('name')} is missing outputSchema.")
            require(tool["outputSchema"].get("type") == "object", f"Tool {tool.get('name')} outputSchema is not an object schema.")
            annotations = tool.get("annotations")
            require(isinstance(annotations, dict), f"Tool {tool.get('name')} is missing annotations.")
            for hint in ("readOnlyHint", "destructiveHint", "idempotentHint", "openWorldHint"):
                require(hint in annotations and type(annotations[hint]) is bool, f"Tool {tool.get('name')} has invalid annotation {hint}.")
            require(annotations["openWorldHint"] is False, f"Tool {tool.get('name')} must declare openWorldHint=false.")
        require(surface[2]["result"].get("isError") is not True, "tools/list returned a tool error.")
        setting = structured_result(surface, 5, "project/get_setting")
        require(setting.get("value") == "MCP Python Smoke A", "project/get_setting returned the wrong value.")
        require(not str(setting.get("value", "")).startswith("s:"), "project/get_setting returned a legacy s: envelope.")
        created = structured_result(surface, 3, "script/create")
        require(created.get("sha256") == text_sha256(INITIAL_SCRIPT_TEXT), "script/create returned the wrong SHA-256.")
        require((project_a / "mcp_smoke_created.gd").read_text(encoding="utf-8") == INITIAL_SCRIPT_TEXT, "script/create did not write the requested source.")
        fetched = structured_result(surface, 4, "script/get")
        require(fetched.get("sha256") == created.get("sha256"), "script/get did not read the created script.")
        search = structured_result(surface, 6, "class/search")
        matches = [match for match in search.get("matches", []) if isinstance(match, dict)]
        require(any(match.get("name") == "SmokeUndocumentedClass" for match in matches), "class/search did not find the undocumented class_name script.")
        docs_error = tool_result(surface, 7, "class/get_documentation")
        require(bool(docs_error.get("isError")), "class/get_documentation unexpectedly succeeded for an undocumented script class.")
        error = docs_error.get("structuredContent", {}).get("error", {})
        require(error.get("code") == "USE_SCRIPT_DOCUMENTATION_TOOL", "class documentation returned the wrong guidance code.")
        require(tool_error_code(surface, 8, "gdextension/build invalid profile") == "INVALID_ARGUMENTS", "gdextension/build accepted an invalid profile.")
        require(tool_error_code(surface, 9, "gdextension/build missing extension") == "INVALID_EXTENSION_PATH", "gdextension/build returned the wrong missing-path code.")
        require(tool_error_code(surface, 10, "gdextension/build missing SConstruct") == "BUILDER_NOT_FOUND", "gdextension/build returned the wrong missing-builder code.")

        apply_probe = invoke_stdio(
            runner,
            project_a,
            discovery_a["protocolVersion"],
            [
                tool_call(
                    70,
                    "godot.project.apply",
                    {
                        "settings": [
                            {
                                "operation": "set",
                                "name": "mcp/smoke_apply_probe",
                                "value": 17,
                            }
                        ],
                        "save": False,
                    },
                )
            ],
            "project apply",
        )
        apply_result = structured_result(apply_probe, 70, "project/apply")
        require(apply_result.get("changed") is True, "project/apply did not report the setting change.")
        require(apply_result.get("saved") is False, "project/apply unexpectedly saved the smoke project.")
        revision = apply_result.get("projectRevision")
        require(isinstance(revision, str) and len(revision) == 64 and all(character in "0123456789abcdef" for character in revision), "project/apply revision was not a lowercase SHA-256 string.")

        rename_prep = invoke_stdio(
            runner,
            project_a,
            discovery_a["protocolVersion"],
            [
                tool_call(8, "godot.script.get", {"path": "res://mcp_smoke_created.gd"}),
                tool_call(
                    9,
                    "godot.gdscript.rename",
                    {
                        "path": "res://mcp_smoke_created.gd",
                        "line": 4,
                        "character": 5,
                        "newName": "renamed_value",
                    },
                ),
            ],
            "stdio workspace edit preparation",
        )
        rename_snapshot = structured_result(rename_prep, 8, "script/get before workspace edit")
        rename_result = structured_result(rename_prep, 9, "gdscript/rename")
        require(bool(rename_result.get("changed")), "gdscript/rename returned an empty WorkspaceEdit.")
        workspace_edit = rename_result.get("edit")
        require(isinstance(workspace_edit, dict), "gdscript/rename did not return a WorkspaceEdit object.")
        unchanged_sha = rename_snapshot.get("sha256")
        require(isinstance(unchanged_sha, str) and unchanged_sha, "script/get returned no SHA-256 before workspace edit.")

        negative_apply = invoke_stdio(
            runner,
            project_a,
            discovery_a["protocolVersion"],
            [
                tool_call(
                    10,
                    "godot.gdscript.apply_workspace_edit",
                    {
                        "edit": workspace_edit,
                        "documents": [
                            {
                                "path": "res://node_script.gd",
                                "expected_sha256": text_sha256((project_a / "node_script.gd").read_text(encoding="utf-8")),
                            }
                        ],
                    },
                ),
                tool_call(
                    11,
                    "godot.gdscript.apply_workspace_edit",
                    {
                        "edit": workspace_edit,
                        "documents": [
                            {"path": "res://mcp_smoke_created.gd", "expected_sha256": unchanged_sha},
                            {
                                "path": "res://node_script.gd",
                                "expected_sha256": text_sha256((project_a / "node_script.gd").read_text(encoding="utf-8")),
                            },
                        ],
                    },
                ),
                tool_call(
                    12,
                    "godot.gdscript.apply_workspace_edit",
                    {
                        "edit": workspace_edit,
                        "documents": [
                            {"path": "res://mcp_smoke_created.gd", "expected_sha256": unchanged_sha},
                            {"path": "res://mcp_smoke_created.gd", "expected_sha256": unchanged_sha},
                        ],
                    },
                ),
                tool_call(
                    13,
                    "godot.gdscript.apply_workspace_edit",
                    {
                        "edit": workspace_edit,
                        "documents": [{"path": "res://mcp_smoke_created.gd", "expected_sha256": "0" * 64}],
                    },
                ),
                tool_call(14, "godot.script.get", {"path": "res://mcp_smoke_created.gd"}),
            ],
            "stdio workspace edit preflight failures",
        )
        require(tool_error_code(negative_apply, 10, "workspace edit missing expectation") == "MISSING_EXPECTATION", "missing expectation returned the wrong error code.")
        require(tool_error_code(negative_apply, 11, "workspace edit unused expectation") == "UNUSED_EXPECTATION", "unused expectation returned the wrong error code.")
        require(tool_error_code(negative_apply, 12, "workspace edit duplicate expectation") == "INVALID_ARGUMENTS", "duplicate expectation returned the wrong error code.")
        require(tool_error_code(negative_apply, 13, "workspace edit stale revision") == "stale_revision", "stale expectation returned the wrong error code.")
        after_preflight = structured_result(negative_apply, 14, "script/get after workspace edit preflight")
        require(after_preflight.get("sha256") == unchanged_sha, "workspace edit preflight failure changed the ScriptEditor buffer.")

        apply_flow = invoke_stdio(
            runner,
            project_a,
            discovery_a["protocolVersion"],
            [
                tool_call(
                    15,
                    "godot.gdscript.apply_workspace_edit",
                    {
                        "edit": workspace_edit,
                        "documents": [{"path": "res://mcp_smoke_created.gd", "expected_sha256": unchanged_sha}],
                    },
                )
            ],
            "stdio workspace edit apply",
        )
        apply_result = structured_result(apply_flow, 15, "gdscript/apply_workspace_edit")
        require(bool(apply_result.get("applied")) and not bool(apply_result.get("saved")), "workspace edit did not remain unsaved.")
        require(apply_result.get("documentCount") == 1, "workspace edit changed an unexpected number of documents.")
        applied_documents = apply_result.get("documents")
        require(isinstance(applied_documents, list) and len(applied_documents) == 1, "workspace edit returned invalid document results.")
        applied_sha = applied_documents[0].get("sha256")
        require(isinstance(applied_sha, str) and applied_sha != unchanged_sha, "workspace edit did not change the authoritative SHA-256.")
        require((project_a / "mcp_smoke_created.gd").read_text(encoding="utf-8") == INITIAL_SCRIPT_TEXT, "workspace edit saved the script implicitly.")

        restore_flow = invoke_stdio(
            runner,
            project_a,
            discovery_a["protocolVersion"],
            [
                tool_call(16, "godot.script.get", {"path": "res://mcp_smoke_created.gd"}),
                tool_call(
                    17,
                    "godot.script.edit",
                    {
                        "path": "res://mcp_smoke_created.gd",
                        "text": INITIAL_SCRIPT_TEXT,
                        "expected_sha256": applied_sha,
                    },
                ),
                tool_call(18, "godot.script.save", {"path": "res://mcp_smoke_created.gd"}),
            ],
            "stdio workspace edit restore",
        )
        renamed_document = structured_result(restore_flow, 16, "script/get after workspace edit")
        properties = renamed_document.get("properties")
        require(
            isinstance(properties, list)
            and any(isinstance(item, dict) and item.get("text") == "var renamed_value: int = 7" for item in properties),
            "workspace edit was not authoritative in ScriptEditor.",
        )
        structured_result(restore_flow, 17, "script/edit workspace restore")
        saved_restore = structured_result(restore_flow, 18, "script/save workspace restore")
        require(saved_restore.get("sha256") == text_sha256(INITIAL_SCRIPT_TEXT), "workspace edit restore saved unexpected source.")

        print("[7/9] Checking node create, undo/redo, and explicit scene save")
        wait_for_file(host_a, ".mcp-smoke-scene-ready", timeout)
        node_requests = [
            tool_call(20, "godot.node.create", {"type": "Node", "name": "McpChild", "parentPath": "."}),
            tool_call(21, "godot.scene.get_tree", {"maxDepth": 3}),
            tool_call(22, "godot.editor.undo", {}),
            tool_call(23, "godot.scene.get_tree", {"maxDepth": 3}),
            tool_call(24, "godot.editor.redo", {}),
            tool_call(25, "godot.scene.get_tree", {"maxDepth": 3}),
            tool_call(26, "godot.scene.save", {}),
        ]
        node_flow = invoke_stdio(runner, project_a, discovery_a["protocolVersion"], node_requests, "stdio node lifecycle")
        created_node = structured_result(node_flow, 20, "node/create")
        require(created_node.get("path") == "McpChild", "node/create returned the wrong path.")
        require("McpChild" in children_names(structured_result(node_flow, 21, "scene/get_tree after create")), "created node is missing from the scene tree.")
        require(bool(structured_result(node_flow, 22, "editor/undo").get("performed")), "editor/undo did not report a performed action.")
        require("McpChild" not in children_names(structured_result(node_flow, 23, "scene/get_tree after undo")), "undo left the created node in the scene tree.")
        require(bool(structured_result(node_flow, 24, "editor/redo").get("performed")), "editor/redo did not report a performed action.")
        require("McpChild" in children_names(structured_result(node_flow, 25, "scene/get_tree after redo")), "redo did not restore the created node.")
        require(bool(structured_result(node_flow, 26, "scene/save").get("saved")), "scene/save did not report success.")
        require("McpChild" in (project_a / "main.tscn").read_text(encoding="utf-8"), "scene/save did not persist the created node.")

        print("[8/9] Checking runtime play, native properties, input, performance, debug errors, and stop")
        play_flow = invoke_stdio(runner, project_a, discovery_a["protocolVersion"], [tool_call(30, "godot.runtime.play", {"mode": "main"})], "runtime/play")
        require(bool(structured_result(play_flow, 30, "runtime/play").get("accepted")), "runtime/play was not accepted.")

        def running_state() -> Optional[Dict[str, Any]]:
            state_flow = invoke_stdio(runner, project_a, discovery_a["protocolVersion"], [tool_call(31, "godot.runtime.get_state", {})], "runtime/get_state while starting")
            state = structured_result(state_flow, 31, "runtime/get_state")
            if bool(state.get("playing")) and int(state.get("sessionCount", 0)) > 0:
                return state
            return None

        runtime_state = wait_until(timeout, running_state, "running runtime state")
        require(type(runtime_state.get("processId")) is int and runtime_state["processId"] > 0, "runtime processId must be a positive JSON integer.")
        runtime_generation = runtime_state["sessions"][0].get("runtimeGeneration")
        require(type(runtime_generation) is int and runtime_generation > 0, "runtime session did not expose runtimeGeneration.")

        runtime_mutation_flow = invoke_stdio(
            runner,
            project_a,
            discovery_a["protocolVersion"],
            [
                tool_call(34, "godot.runtime.node.set_property", {
                    "path": "/root/Root/RuntimeParent/RuntimeChild",
                    "property": "global_position",
                    "value": {"__godot_mcp_encoded_variant__": {"type": "Vector2", "args": [1.05, -2.25]}},
                    "runtimeGeneration": runtime_generation,
                }),
                tool_call(35, "godot.runtime.input.send", {
                    "events": [{"type": "action", "action": "mcp-python-runtime-action", "pressed": True}],
                    "runtimeGeneration": runtime_generation,
                }),
                tool_call(36, "godot.runtime.performance.start", {"name": "stdio-performance", "maxFrames": 1, "runtimeGeneration": runtime_generation}),
                tool_call(37, "godot.runtime.performance.status", {"name": "stdio-performance", "runtimeGeneration": runtime_generation}),
                tool_call(38, "godot.runtime.performance.stop", {"name": "stdio-performance", "runtimeGeneration": runtime_generation}),
            ],
            "stdio runtime mutation",
        )
        runtime_property = structured_result(runtime_mutation_flow, 34, "runtime/node/set_property")
        require(runtime_property.get("property") == "global_position", "runtime native property fallback did not return the requested property.")
        encoded_position = runtime_property.get("value", {}).get("__godot_mcp_encoded_variant__", {})
        require(encoded_position.get("type") == "Vector2", "runtime native property fallback did not return a Vector2.")
        position_args = encoded_position.get("args")
        require(isinstance(position_args, list) and len(position_args) == 2, "runtime global_position did not return two components.")
        require(all(isinstance(value, (int, float)) for value in position_args), "runtime global_position components are not JSON numbers.")
        require(abs(float(position_args[0]) - 1.05) < 0.001 and abs(float(position_args[1]) + 2.25) < 0.001, "runtime global_position was not updated.")
        require(int(structured_result(runtime_mutation_flow, 35, "runtime/input/send").get("eventCount", 0)) == 1, "runtime action injection was rejected.")
        performance_start = structured_result(runtime_mutation_flow, 36, "runtime/performance/start")
        performance_status = structured_result(runtime_mutation_flow, 37, "runtime/performance/status")
        performance_stop = structured_result(runtime_mutation_flow, 38, "runtime/performance/stop")
        require(performance_start.get("name") == "stdio-performance", "performance start did not return its client alias.")
        require(performance_status.get("jobId") == performance_start.get("jobId"), "performance status did not resolve the client alias.")
        require(performance_stop.get("jobId") == performance_start.get("jobId"), "performance stop did not resolve the client alias.")

        def runtime_errors() -> Optional[Dict[str, Any]]:
            errors_flow = invoke_stdio(
                runner,
                project_a,
                discovery_a["protocolVersion"],
                [tool_call(32, "godot.debug.get_errors", {"source": "runtime", "query": "mcp-python-smoke-runtime-", "includeWarnings": True})],
                "debug/get_errors while running",
            )
            errors = structured_result(errors_flow, 32, "debug/get_errors")
            messages = {str(item.get("message")) for item in errors.get("errors", []) if isinstance(item, dict)}
            return errors if {"mcp-python-smoke-runtime-warning", "mcp-python-smoke-runtime-error"} <= messages else None

        wait_until(timeout, runtime_errors, "runtime warning and error capture")
        stop_flow = invoke_stdio(runner, project_a, discovery_a["protocolVersion"], [tool_call(40, "godot.runtime.stop", {})], "runtime/stop")
        require(bool(structured_result(stop_flow, 40, "runtime/stop").get("stopped")), "runtime/stop did not stop the running project.")

        def stopped_state() -> Optional[Dict[str, Any]]:
            state_flow = invoke_stdio(runner, project_a, discovery_a["protocolVersion"], [tool_call(41, "godot.runtime.get_state", {})], "runtime/get_state after stop")
            state = structured_result(state_flow, 41, "runtime/get_state after stop")
            return state if not bool(state.get("playing")) else None

        wait_until(timeout, stopped_state, "runtime stop state")

        print("[9/9] Rechecking project B isolation and graceful stop markers")
        discovery_b_after = wait_for_discovery(runner, host_b, timeout)
        require(discovery_b_after["instanceId"] == discovery_b["instanceId"], "project A traffic changed project B routing.")
        for host in reversed(hosts):
            host.stop(require_graceful=True)
        success = True
        print("MCP Python CLI smoke test passed.")
    finally:
        for host in reversed(hosts):
            try:
                host.stop(require_graceful=False)
            except Exception as error:  # Keep the original smoke failure visible.
                print(f"warning: failed to clean up {host.label} Host: {error}", file=sys.stderr)
        if keep_temporary_projects:
            print(f"Temporary projects kept at: {temporary_root}")
        else:
            from mcp_smoke_test_utils import remove_tree_with_retry

            remove_tree_with_retry(temporary_root)
    require(success, "smoke test did not reach its completion checks.")


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Cross-platform smoke test for the embedded Godot MCP CLI and editor Host.")
    parser.add_argument("--binary", required=True, help="Godot editor binary built with MCP support.")
    parser.add_argument("--timeout-seconds", type=int, default=45, help="Per-operation smoke timeout (5-300 seconds).")
    parser.add_argument("--keep-temporary-projects", action="store_true", help="Keep generated projects for failure investigation.")
    args = parser.parse_args(argv)
    if not 5 <= args.timeout_seconds <= 300:
        parser.error("--timeout-seconds must be between 5 and 300")
    return args


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    try:
        run_smoke(args.binary, args.timeout_seconds, args.keep_temporary_projects)
    except SmokeFailure as error:
        print(str(error), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("MCP Python CLI smoke test interrupted.", file=sys.stderr)
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
