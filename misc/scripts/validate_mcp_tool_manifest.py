#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path


DEFAULT_MANIFEST = Path("tests/editor/mcp/data/mcp_tool_manifest.json")
DEFAULT_EXPECTED_TOOL_COUNT = 110
TOOL_NAME = re.compile(r"godot(?:\.[a-z][a-z0-9_]*){2,}")


@dataclass(frozen=True, order=True)
class ToolManifestViolation:
    path: str
    rule: str
    reason: str

    def format(self) -> str:
        return f"{self.path}: MCP tool manifest rule '{self.rule}' violated: {self.reason}"


def validate_mcp_tool_manifest(
    repository_root: Path,
    manifest_path: Path = DEFAULT_MANIFEST,
    expected_tool_count: int = DEFAULT_EXPECTED_TOOL_COUNT,
) -> list[ToolManifestViolation]:
    repository_root = repository_root.resolve()
    path = manifest_path if manifest_path.is_absolute() else repository_root / manifest_path
    display_path = path.relative_to(repository_root).as_posix() if path.is_relative_to(repository_root) else str(path)

    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return [ToolManifestViolation(display_path, "file", "manifest does not exist")]
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        return [ToolManifestViolation(display_path, "json", f"manifest is not valid UTF-8 JSON: {error}")]

    violations: list[ToolManifestViolation] = []
    if not isinstance(manifest, dict):
        return [ToolManifestViolation(display_path, "root", "manifest root must be an object")]

    if set(manifest) != {"schemaVersion", "tools"}:
        violations.append(
            ToolManifestViolation(
                display_path,
                "fields",
                "manifest fields must be exactly schemaVersion and tools",
            )
        )
    if manifest.get("schemaVersion") != 1:
        violations.append(ToolManifestViolation(display_path, "schema-version", "schemaVersion must be 1"))

    tools = manifest.get("tools")
    if not isinstance(tools, list):
        violations.append(ToolManifestViolation(display_path, "tools", "tools must be an array"))
        return sorted(violations)
    if len(tools) != expected_tool_count:
        violations.append(
            ToolManifestViolation(
                display_path,
                "tool-count",
                f"expected {expected_tool_count} tools, found {len(tools)}",
            )
        )

    seen: set[str] = set()
    for index, name in enumerate(tools):
        if not isinstance(name, str) or TOOL_NAME.fullmatch(name) is None:
            violations.append(
                ToolManifestViolation(
                    display_path,
                    "tool-name",
                    f"tools[{index}] must be a canonical godot namespace name",
                )
            )
            continue
        if name in seen:
            violations.append(
                ToolManifestViolation(display_path, "duplicate-tool", f"tool name is repeated: {name}")
            )
        seen.add(name)

    return sorted(violations)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the embedded MCP ordered tool compatibility snapshot.")
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent.parent,
        help="Godot repository root (defaults to the root containing this script).",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help="Manifest path relative to the repository root.",
    )
    parser.add_argument(
        "--expected-tool-count",
        type=int,
        default=DEFAULT_EXPECTED_TOOL_COUNT,
        help="Expected public tool count. Change only with an intentional compatibility update.",
    )
    args = parser.parse_args()
    if args.expected_tool_count < 1:
        parser.error("--expected-tool-count must be positive")

    violations = validate_mcp_tool_manifest(args.root, args.manifest, args.expected_tool_count)
    for violation in violations:
        print(violation.format(), file=sys.stderr)
    if violations:
        print(f"MCP tool manifest: {len(violations)} violation(s).", file=sys.stderr)
        return 1

    print(f"MCP tool manifest: OK ({args.expected_tool_count} ordered tools).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
