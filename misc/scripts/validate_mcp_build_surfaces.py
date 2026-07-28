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
    prefixes = None
    has_enabled_assignment = False
    guarded_uses = 0
    for candidate in ast.walk(module):
        if isinstance(candidate, ast.Assign) and len(candidate.targets) == 1 and _is_name(
            candidate.targets[0], "MCP_TEST_PREFIXES"
        ):
            if isinstance(candidate.value, (ast.List, ast.Tuple)):
                prefixes = tuple(item.value for item in candidate.value.elts if isinstance(item, ast.Constant))
        if isinstance(candidate, ast.Assign) and len(candidate.targets) == 1 and _is_name(
            candidate.targets[0], "editor_mcp_enabled"
        ):
            has_enabled_assignment = _contains(
                candidate.value, lambda node: _is_attribute(node, "env", "editor_build")
            ) and _contains(candidate.value, lambda node: _is_subscript(node, "env", "mcp"))
        if isinstance(candidate, ast.If) and _contains(candidate.test, lambda node: _is_name(node, "editor_mcp_enabled")):
            guarded_uses += 1
    return (
        prefixes == ("core/mcp/", "editor/mcp/", "main/test_mcp_")
        and has_enabled_assignment
        and guarded_uses >= 2
    )


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
                "mcp-mode-resolution",
                _has_mcp_mode_resolution(sconstruct),
                "mcp=auto must resolve from the effective editor target after profile loading",
            ),
            (
                "release-rejection",
                _has_release_rejection(sconstruct),
                "release templates must reject an enabled MCP build",
            ),
            ("mcp-define", _has_mcp_define(sconstruct), "enabled MCP builds must define MCP_ENABLED"),
        )
        for gate, valid, reason in requirements:
            if not valid:
                violations.append(BuildSurfaceViolation("SConstruct", gate, reason))

    core = modules.get("core/SCsub")
    if core and not _has_guarded_call(
        core,
        "SConscript",
        "mcp/SCsub",
        (
            lambda node: _is_attribute(node, "env", "editor_build"),
            lambda node: _is_subscript(node, "env", "mcp"),
        ),
    ):
        violations.append(
            BuildSurfaceViolation("core/SCsub", "core-editor-only", "core/mcp must compile only in MCP-enabled editor builds")
        )

    editor = modules.get("editor/SCsub")
    if editor and not _has_guarded_call(
        editor,
        "SConscript",
        "mcp/SCsub",
        (
            lambda node: _is_attribute(node, "env", "editor_build"),
            lambda node: _is_subscript(node, "env", "mcp"),
        ),
    ):
        violations.append(
            BuildSurfaceViolation("editor/SCsub", "editor-host-gate", "editor/mcp must compile only in MCP-enabled editor builds")
        )

    main = modules.get("main/SCsub")
    if main and not _has_source_filter(
        main,
        (
            lambda node: _is_attribute(node, "env_main", "editor_build"),
            lambda node: _is_subscript(node, "env_main", "mcp"),
        ),
        "mcp_",
    ):
        violations.append(
            BuildSurfaceViolation("main/SCsub", "cli-source-gate", "main/mcp_* must compile only in MCP-enabled editor builds")
        )

    debugger = modules.get("scene/debugger/SCsub")
    if debugger and not _has_source_filter(
        debugger,
        (lambda node: _is_subscript(node, "env", "mcp"),),
        "mcp_runtime_",
    ):
        violations.append(
            BuildSurfaceViolation(
                "scene/debugger/SCsub",
                "runtime-source-gate",
                "scene/debugger/mcp_runtime_* must be excluded when MCP is disabled",
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
