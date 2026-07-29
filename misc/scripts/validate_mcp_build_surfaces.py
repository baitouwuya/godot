#!/usr/bin/env python3

from __future__ import annotations

import argparse
import ast
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable


BUILD_FILES = (
    "SConstruct",
    "core/SCsub",
    "editor/SCsub",
    "main/SCsub",
    "scene/debugger/SCsub",
    "scu_builders.py",
    "tests/SCsub",
)


@dataclass(frozen=True, order=True)
class BuildSurfaceViolation:
    path: str
    gate: str
    reason: str

    def format(self) -> str:
        return f"{self.path}: MCP build surface gate '{self.gate}' violated: {self.reason}"


NodePredicate = Callable[[ast.AST], bool]


def _is_name(node: ast.AST, name: str) -> bool:
    return isinstance(node, ast.Name) and node.id == name


def _is_attribute(node: ast.AST, owner: str, attribute: str) -> bool:
    return isinstance(node, ast.Attribute) and _is_name(node.value, owner) and node.attr == attribute


def _is_subscript(node: ast.AST, owner: str, key: str) -> bool:
    return (
        isinstance(node, ast.Subscript)
        and _is_name(node.value, owner)
        and isinstance(node.slice, ast.Constant)
        and node.slice.value == key
    )


def _contains(node: ast.AST, predicate: NodePredicate) -> bool:
    return any(predicate(candidate) for candidate in ast.walk(node))


def _contains_constant(node: ast.AST, value: object) -> bool:
    return _contains(node, lambda candidate: isinstance(candidate, ast.Constant) and candidate.value == value)


def _call_name(call: ast.Call) -> str:
    if isinstance(call.func, ast.Name):
        return call.func.id
    if isinstance(call.func, ast.Attribute):
        return call.func.attr
    return ""


def _contains_call(nodes: Iterable[ast.AST], name: str, first_argument: object | None = None) -> bool:
    for node in nodes:
        for candidate in ast.walk(node):
            if not isinstance(candidate, ast.Call) or _call_name(candidate) != name:
                continue
            if first_argument is None:
                return True
            if candidate.args and isinstance(candidate.args[0], ast.Constant) and candidate.args[0].value == first_argument:
                return True
    return False


def _guarded_calls(statements: Iterable[ast.stmt], guards: tuple[ast.AST, ...] = ()):
    for statement in statements:
        if isinstance(statement, ast.If):
            yield from _guarded_calls(statement.body, guards + (statement.test,))
            negated = ast.UnaryOp(op=ast.Not(), operand=statement.test)
            yield from _guarded_calls(statement.orelse, guards + (negated,))
            continue
        for candidate in ast.walk(statement):
            if isinstance(candidate, ast.Call):
                yield candidate, guards


def _has_guarded_call(
    module: ast.Module,
    name: str,
    first_argument: object,
    guard_predicates: tuple[NodePredicate, ...],
) -> bool:
    for call, guards in _guarded_calls(module.body):
        if _call_name(call) != name or not call.args:
            continue
        if not isinstance(call.args[0], ast.Constant) or call.args[0].value != first_argument:
            continue
        if all(any(_contains(guard, predicate) for guard in guards) for predicate in guard_predicates):
            return True
    return False


def _has_mcp_option(module: ast.Module) -> bool:
    for candidate in ast.walk(module):
        if not isinstance(candidate, ast.Call) or _call_name(candidate) != "EnumVariable" or len(candidate.args) < 4:
            continue
        if not isinstance(candidate.args[0], ast.Constant) or candidate.args[0].value != "mcp":
            continue
        default = candidate.args[2]
        choices = candidate.args[3]
        if not isinstance(default, ast.Constant) or default.value != "auto":
            continue
        if not isinstance(choices, (ast.List, ast.Tuple)):
            continue
        values = {item.value for item in choices.elts if isinstance(item, ast.Constant)}
        if values == {"auto", "no", "yes"}:
            return True
    return False


def _has_profile_aware_option_loading(module: ast.Module) -> bool:
    for candidate in ast.walk(module):
        if not isinstance(candidate, ast.Assign) or len(candidate.targets) != 1:
            continue
        if not _is_name(candidate.targets[0], "opts") or not isinstance(candidate.value, ast.Call):
            continue
        call = candidate.value
        if _call_name(call) != "Variables" or len(call.args) < 2:
            continue
        if _is_name(call.args[0], "customs") and _is_name(call.args[1], "ARGUMENTS"):
            return True
    return False


def _has_mcp_mode_resolution(module: ast.Module) -> bool:
    for candidate in ast.walk(module):
        if not isinstance(candidate, ast.Assign) or len(candidate.targets) != 1:
            continue
        if not _is_subscript(candidate.targets[0], "env", "mcp") or not isinstance(candidate.value, ast.IfExp):
            continue
        value = candidate.value
        if (
            _is_attribute(value.body, "env", "editor_build")
            and _contains_constant(value.test, "auto")
            and _contains_constant(value.orelse, "yes")
        ):
            return True
    return False


def _has_mcp_surface_resolution(module: ast.Module) -> bool:
    editor_enabled = False
    runtime_enabled = False
    for candidate in ast.walk(module):
        if not isinstance(candidate, ast.Assign) or len(candidate.targets) != 1:
            continue
        target = candidate.targets[0]
        if _is_attribute(target, "env", "mcp_editor_enabled"):
            editor_enabled = _contains(
                candidate.value, lambda node: _is_attribute(node, "env", "editor_build")
            ) and _contains(candidate.value, lambda node: _is_subscript(node, "env", "mcp"))
        if _is_attribute(target, "env", "mcp_runtime_enabled"):
            runtime_enabled = _contains(
                candidate.value, lambda node: _is_attribute(node, "env", "debug_features")
            ) and _contains(candidate.value, lambda node: _is_subscript(node, "env", "mcp"))
    return editor_enabled and runtime_enabled


def _has_scu_mcp_runtime_mode(module: ast.Module) -> bool:
    for candidate in ast.walk(module):
        if not isinstance(candidate, ast.Call) or _call_name(candidate) != "generate_scu_files":
            continue
        if any(_contains(argument, lambda node: _is_attribute(node, "env", "mcp_runtime_enabled")) for argument in candidate.args):
            return True
    return False


def _has_release_rejection(module: ast.Module) -> bool:
    for candidate in ast.walk(module):
        if not isinstance(candidate, ast.If):
            continue
        has_release = _contains(candidate.test, lambda node: _is_subscript(node, "env", "target")) and _contains_constant(
            candidate.test, "template_release"
        )
        has_mcp = _contains(candidate.test, lambda node: _is_subscript(node, "env", "mcp"))
        if has_release and has_mcp and _contains_call(candidate.body, "Exit", 255):
            return True
    return False


def _has_mcp_define(module: ast.Module) -> bool:
    for candidate in ast.walk(module):
        if not isinstance(candidate, ast.If) or not _contains(
            candidate.test, lambda node: _is_subscript(node, "env", "mcp")
        ):
            continue
        for body_node in candidate.body:
            for call in ast.walk(body_node):
                if isinstance(call, ast.Call) and _call_name(call) == "Append" and _contains_constant(call, "MCP_ENABLED"):
                    return True
    return False


def _has_source_filter(
    module: ast.Module,
    guard_predicates: tuple[NodePredicate, ...],
    excluded_prefix: str,
) -> bool:
    for candidate in ast.walk(module):
        if not isinstance(candidate, ast.If):
            continue
        if not all(_contains(candidate.test, predicate) for predicate in guard_predicates):
            continue
        if not _contains_call(candidate.body, "add_source_files"):
            continue
        if _contains_constant(ast.Module(body=candidate.orelse, type_ignores=[]), excluded_prefix) and _contains_call(
            candidate.orelse, "startswith", excluded_prefix
        ):
            return True
    return False


def _has_test_gate(module: ast.Module) -> bool:
    editor_prefixes = None
    runtime_sources = None
    has_editor_enabled_assignment = False
    has_runtime_enabled_assignment = False
    has_enabled_filter = False
    has_mcp_selection = False
    has_scu_aggregation = False
    has_enabled_predicate = False
    for candidate in ast.walk(module):
        if isinstance(candidate, ast.Assign) and len(candidate.targets) == 1 and _is_name(
            candidate.targets[0], "MCP_EDITOR_TEST_PREFIXES"
        ):
            if isinstance(candidate.value, (ast.List, ast.Tuple)):
                editor_prefixes = tuple(item.value for item in candidate.value.elts if isinstance(item, ast.Constant))
        if isinstance(candidate, ast.Assign) and len(candidate.targets) == 1 and _is_name(
            candidate.targets[0], "MCP_RUNTIME_TEST_SOURCES"
        ):
            if isinstance(candidate.value, (ast.List, ast.Tuple)):
                runtime_sources = tuple(item.value for item in candidate.value.elts if isinstance(item, ast.Constant))
        if isinstance(candidate, ast.Assign) and len(candidate.targets) == 1 and _is_name(
            candidate.targets[0], "editor_mcp_enabled"
        ):
            has_editor_enabled_assignment = _contains(
                candidate.value, lambda node: _is_attribute(node, "env", "mcp_editor_enabled")
            )
        if isinstance(candidate, ast.Assign) and len(candidate.targets) == 1 and _is_name(
            candidate.targets[0], "runtime_mcp_enabled"
        ):
            has_runtime_enabled_assignment = _contains(
                candidate.value, lambda node: _is_attribute(node, "env", "mcp_runtime_enabled")
            )
        if isinstance(candidate, ast.Assign) and len(candidate.targets) == 1 and _is_name(
            candidate.targets[0], "force_link_sources"
        ):
            has_enabled_filter |= isinstance(candidate.value, ast.ListComp) and _contains_call(
                (candidate.value,), "is_enabled_test_source"
            )
        if isinstance(candidate, ast.Assign) and len(candidate.targets) == 1 and _is_name(
            candidate.targets[0], "mcp_test_sources"
        ):
            has_mcp_selection = isinstance(candidate.value, ast.ListComp) and _contains_call(
                (candidate.value,), "is_mcp_test_source"
            )
        if isinstance(candidate, ast.FunctionDef) and candidate.name == "is_enabled_test_source":
            has_enabled_predicate = (
                _contains(candidate, lambda node: _is_name(node, "editor_mcp_enabled"))
                and _contains(candidate, lambda node: _is_name(node, "runtime_mcp_enabled"))
                and _contains_call((candidate,), "is_editor_mcp_test_source")
                and _contains_call((candidate,), "is_runtime_mcp_test_source")
            )
        if isinstance(candidate, ast.If) and _contains(candidate.test, lambda node: _is_subscript(node, "env", "scu_build")):
            for call in (node for node in ast.walk(ast.Module(body=candidate.body, type_ignores=[])) if isinstance(node, ast.Call)):
                if _call_name(call) == "add_source_files" and len(call.args) >= 2 and _contains(
                    call.args[1], lambda node: _is_name(node, "mcp_test_sources")
                ):
                    has_scu_aggregation = True
    return (
        editor_prefixes == ("core/mcp/", "editor/mcp/", "main/test_mcp_")
        and runtime_sources
        == (
            "editor/mcp/test_mcp_runtime_condition_scheduler.cpp",
            "editor/mcp/test_mcp_runtime_input_controller.cpp",
            "editor/mcp/test_mcp_runtime_performance_sampler.cpp",
        )
        and has_editor_enabled_assignment
        and has_runtime_enabled_assignment
        and has_enabled_filter
        and has_mcp_selection
        and has_scu_aggregation
        and has_enabled_predicate
    )


def _has_scu_runtime_filter(module: ast.Module) -> bool:
    has_exclusion_parameter = False
    has_runtime_mode = False
    has_scene_debugger_filter = False
    for candidate in ast.walk(module):
        if isinstance(candidate, ast.FunctionDef) and candidate.name == "find_files_in_folder":
            has_exclusion_parameter = any(argument.arg == "excluded_file_prefixes" for argument in candidate.args.args)
            has_exclusion_parameter &= _contains_call((candidate,), "startswith") and _contains(
                candidate, lambda node: _is_name(node, "excluded_file_prefixes")
            )
        if isinstance(candidate, ast.FunctionDef) and candidate.name == "generate_scu_files":
            has_runtime_mode = any(argument.arg == "mcp_runtime_enabled" for argument in candidate.args.args)
            for call in (node for node in ast.walk(candidate) if isinstance(node, ast.Call)):
                if _call_name(call) != "process_folder":
                    continue
                if (
                    _contains_constant(call, "scene/debugger")
                    and _contains_constant(call, "mcp_runtime_")
                    and _contains(call, lambda node: _is_name(node, "mcp_runtime_enabled"))
                ):
                    has_scene_debugger_filter = True
    return has_exclusion_parameter and has_runtime_mode and has_scene_debugger_filter


def _has_scu_test_coverage(module: ast.Module) -> bool:
    for candidate in ast.walk(module):
        if not isinstance(candidate, ast.FunctionDef) or candidate.name != "generate_scu_files":
            continue
        for call in (node for node in ast.walk(candidate) if isinstance(node, ast.Call)):
            if _call_name(call) == "process_folder" and _contains_constant(call, "tests") and _contains_constant(
                call, "/core/debugger"
            ):
                return True
    return False


def _parse_build_files(repository_root: Path) -> tuple[dict[str, ast.Module], list[BuildSurfaceViolation]]:
    modules: dict[str, ast.Module] = {}
    violations: list[BuildSurfaceViolation] = []
    for relative_path in BUILD_FILES:
        path = repository_root / relative_path
        try:
            modules[relative_path] = ast.parse(path.read_text(encoding="utf-8"), filename=relative_path)
        except FileNotFoundError:
            violations.append(BuildSurfaceViolation(relative_path, "required-file", "required build file is missing"))
        except (OSError, UnicodeError, SyntaxError) as error:
            violations.append(BuildSurfaceViolation(relative_path, "parse", f"cannot parse build file: {error}"))
    return modules, violations


def validate_mcp_build_surfaces(repository_root: Path) -> list[BuildSurfaceViolation]:
    repository_root = repository_root.resolve()
    modules, violations = _parse_build_files(repository_root)

    sconstruct = modules.get("SConstruct")
    if sconstruct:
        requirements = (
            ("mcp-option", _has_mcp_option(sconstruct), "mcp must remain an auto|no|yes option with auto as its default"),
            (
                "mcp-option-precedence",
                _has_profile_aware_option_loading(sconstruct),
                "custom.py and profiles must load through Variables with explicit command-line arguments taking precedence",
            ),
            (
                "mcp-mode-resolution",
                _has_mcp_mode_resolution(sconstruct),
                "mcp=auto must resolve from the effective editor target after profile loading",
            ),
            (
                "mcp-surface-resolution",
                _has_mcp_surface_resolution(sconstruct),
                "editor and runtime MCP build surfaces must derive from the resolved target and MCP mode",
            ),
            (
                "release-rejection",
                _has_release_rejection(sconstruct),
                "release templates must reject an enabled MCP build",
            ),
            ("mcp-define", _has_mcp_define(sconstruct), "enabled MCP builds must define MCP_ENABLED"),
            (
                "scu-runtime-mode",
                _has_scu_mcp_runtime_mode(sconstruct),
                "SCU generation must receive the resolved MCP runtime build surface",
            ),
        )
        for gate, valid, reason in requirements:
            if not valid:
                violations.append(BuildSurfaceViolation("SConstruct", gate, reason))

    core = modules.get("core/SCsub")
    if core and not _has_guarded_call(
        core,
        "SConscript",
        "mcp/SCsub",
        (lambda node: _is_attribute(node, "env", "mcp_editor_enabled"),),
    ):
        violations.append(
            BuildSurfaceViolation("core/SCsub", "core-editor-only", "core/mcp must compile only in MCP-enabled editor builds")
        )

    editor = modules.get("editor/SCsub")
    if editor and not _has_guarded_call(
        editor,
        "SConscript",
        "mcp/SCsub",
        (lambda node: _is_attribute(node, "env", "mcp_editor_enabled"),),
    ):
        violations.append(
            BuildSurfaceViolation("editor/SCsub", "editor-host-gate", "editor/mcp must compile only in MCP-enabled editor builds")
        )

    main = modules.get("main/SCsub")
    if main and not _has_source_filter(
        main,
        (lambda node: _is_attribute(node, "env_main", "mcp_editor_enabled"),),
        "mcp_",
    ):
        violations.append(
            BuildSurfaceViolation("main/SCsub", "cli-source-gate", "main/mcp_* must compile only in MCP-enabled editor builds")
        )

    debugger = modules.get("scene/debugger/SCsub")
    if debugger and not _has_source_filter(
        debugger,
        (
            lambda node: _is_attribute(node, "env", "mcp_runtime_enabled"),
            lambda node: _is_subscript(node, "env", "scu_build"),
        ),
        "mcp_runtime_",
    ):
        violations.append(
            BuildSurfaceViolation(
                "scene/debugger/SCsub",
                "runtime-source-gate",
                "scene/debugger/mcp_runtime_* must be excluded when MCP is disabled",
            )
        )

    scu = modules.get("scu_builders.py")
    if scu and not _has_scu_runtime_filter(scu):
        violations.append(
            BuildSurfaceViolation(
                "scu_builders.py",
                "scu-runtime-source-gate",
                "SCU generation must exclude scene/debugger/mcp_runtime_* when the runtime surface is disabled",
            )
        )
    if scu and not _has_scu_test_coverage(scu):
        violations.append(
            BuildSurfaceViolation(
                "scu_builders.py",
                "scu-test-coverage",
                "SCU test aggregation must include tests/core/debugger so force-linked tests have definitions",
            )
        )

    tests = modules.get("tests/SCsub")
    if tests and not _has_test_gate(tests):
        violations.append(
            BuildSurfaceViolation(
                "tests/SCsub",
                "test-source-gate",
                "editor MCP tests and SCU force-link sources must follow the editor MCP product surface",
            )
        )

    return sorted(violations)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the embedded MCP SCons build surfaces.")
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent.parent,
        help="Godot repository root (defaults to the root containing this script).",
    )
    args = parser.parse_args()

    violations = validate_mcp_build_surfaces(args.root)
    for violation in violations:
        print(violation.format(), file=sys.stderr)
    if violations:
        print(f"MCP build surfaces: {len(violations)} violation(s).", file=sys.stderr)
        return 1

    print(f"MCP build surfaces: OK ({len(BUILD_FILES)} build files checked).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
