#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc", ".m", ".mm"}
REPOSITORY_INCLUDE_ROOTS = {"core", "editor", "main", "scene"}
RE_INCLUDE = re.compile(r'^[ \t]*#[ \t]*(?:include|import)[ \t]*[<"](?P<path>[^>"]+)[>"]', re.MULTILINE)


@dataclass(frozen=True, order=True)
class DependencyViolation:
    source: str
    line: int
    rule: str
    include: str
    resolved_include: str
    reason: str

    def format(self) -> str:
        return (
            f"{self.source}:{self.line}: MCP dependency boundary '{self.rule}' violated: "
            f"include '{self.include}' resolves to '{self.resolved_include}'; {self.reason}"
        )


def _source_files(directory: Path, pattern: str = "*") -> list[Path]:
    if not directory.is_dir():
        return []
    return sorted(path for path in directory.glob(pattern) if path.is_file() and path.suffix in SOURCE_SUFFIXES)


def _normalize_include(repository_root: Path, source: Path, include: str) -> str:
    include_path = PurePosixPath(include.replace("\\", "/"))
    if include_path.parts and include_path.parts[0] in REPOSITORY_INCLUDE_ROOTS:
        candidate = repository_root.joinpath(*include_path.parts)
    else:
        candidate = source.parent.joinpath(*include_path.parts)
    return Path(os.path.relpath(candidate, repository_root)).as_posix()


def _includes(repository_root: Path, source: Path) -> list[tuple[int, str, str]]:
    content = source.read_text(encoding="utf-8")
    includes = []
    for match in RE_INCLUDE.finditer(content):
        include = match.group("path")
        line = content.count("\n", 0, match.start()) + 1
        includes.append((line, include, _normalize_include(repository_root, source, include)))
    return includes


def _is_transport_or_host_source(source: Path, editor_mcp: Path) -> bool:
    if source.parent != editor_mcp:
        return False
    stem = source.stem
    return stem == "mcp_host" or stem.startswith(("mcp_http_", "mcp_transport_")) or stem.endswith("_transport")


def _add_violation(
    violations: list[DependencyViolation],
    repository_root: Path,
    source: Path,
    line: int,
    rule: str,
    include: str,
    resolved_include: str,
    reason: str,
) -> None:
    violations.append(
        DependencyViolation(
            source.relative_to(repository_root).as_posix(),
            line,
            rule,
            include,
            resolved_include,
            reason,
        )
    )


def validate_mcp_dependencies(repository_root: Path) -> tuple[list[DependencyViolation], int]:
    repository_root = repository_root.resolve()
    core_mcp = repository_root / "core/mcp"
    editor_mcp = repository_root / "editor/mcp"
    providers = editor_mcp / "providers"
    scene_debugger = repository_root / "scene/debugger"

    required_directories = (core_mcp, editor_mcp, providers, scene_debugger)
    missing = [path.relative_to(repository_root).as_posix() for path in required_directories if not path.is_dir()]
    if missing:
        raise ValueError("Repository root is missing required MCP directories: " + ", ".join(missing))

    sources = set(_source_files(core_mcp, "**/*"))
    sources.update(_source_files(editor_mcp))
    sources.update(_source_files(providers, "**/*"))
    sources.update(_source_files(scene_debugger, "mcp_runtime_*"))

    violations: list[DependencyViolation] = []
    for source in sorted(sources):
        source_relative = source.relative_to(repository_root).as_posix()
        for line, include, resolved_include in _includes(repository_root, source):
            if source_relative.startswith("core/mcp/") and resolved_include.startswith(("editor/", "main/", "scene/")):
                _add_violation(
                    violations,
                    repository_root,
                    source,
                    line,
                    "core-mcp-isolation",
                    include,
                    resolved_include,
                    "core/mcp must not depend on editor, main, or scene code",
                )

            if _is_transport_or_host_source(source, editor_mcp) and resolved_include.startswith("editor/mcp/providers/"):
                _add_violation(
                    violations,
                    repository_root,
                    source,
                    line,
                    "transport-host-isolation",
                    include,
                    resolved_include,
                    "editor/mcp transport and host code must not depend on providers",
                )

            if source.parent == providers and source.stem.endswith("_provider"):
                target = PurePosixPath(resolved_include)
                if target.name.endswith("_provider.h") and target.stem != source.stem:
                    _add_violation(
                        violations,
                        repository_root,
                        source,
                        line,
                        "provider-isolation",
                        include,
                        resolved_include,
                        "a provider must not include another provider header",
                    )

            if source.parent == scene_debugger and source.name.startswith("mcp_runtime_") and resolved_include.startswith(
                "editor/"
            ):
                _add_violation(
                    violations,
                    repository_root,
                    source,
                    line,
                    "runtime-editor-isolation",
                    include,
                    resolved_include,
                    "scene/debugger MCP runtime code must not depend on editor code",
                )

    return sorted(violations), len(sources)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate dependency boundaries in the embedded MCP implementation.")
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent.parent,
        help="Godot repository root (defaults to the root containing this script).",
    )
    args = parser.parse_args()

    try:
        violations, source_count = validate_mcp_dependencies(args.root)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"MCP dependency validation failed: {error}", file=sys.stderr)
        return 2

    for violation in violations:
        print(violation.format(), file=sys.stderr)
    if violations:
        print(f"MCP dependency boundaries: {len(violations)} violation(s) in {source_count} source files.", file=sys.stderr)
        return 1

    print(f"MCP dependency boundaries: OK ({source_count} source files checked).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
