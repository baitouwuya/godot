#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Sequence


RUNTIME_SOURCE = "scene/debugger/mcp_runtime_condition_scheduler.cpp"
GDSCRIPT_SEMANTIC_SOURCE = "editor/mcp/providers/mcp_gdscript_semantic_service.cpp"
RELEASE_REJECTION = "The embedded MCP development tools cannot be enabled in release templates."


@dataclass(frozen=True)
class CommandResult:
    command: tuple[str, ...]
    returncode: int
    output: str


def _platform_name() -> str:
    if sys.platform == "darwin":
        return "macos"
    if os.name == "nt":
        return "windows"
    return "linuxbsd"


def _run(root: Path, scons: str, platform: str, label: str, arguments: Sequence[str]) -> CommandResult:
    suffix = f"mcp_config_{label}_{uuid.uuid4().hex[:8]}"
    command = (
        scons,
        "-n",
        f"platform={platform}",
        "progress=no",
        f"extra_suffix={suffix}",
        *arguments,
    )
    process = subprocess.run(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return CommandResult(command, process.returncode, process.stdout)


def _require(condition: bool, message: str, result: CommandResult) -> None:
    if condition:
        return
    command = " ".join(result.command)
    raise RuntimeError(f"{message}\ncommand: {command}\noutput:\n{result.output}")


def run_checks(root: Path, scons: str, platform: str) -> None:
    with tempfile.TemporaryDirectory(prefix="godot-mcp-scons-config-") as temporary_directory:
        profile = Path(temporary_directory) / "mcp_profile.py"
        profile.write_text('target = "template_debug"\nmcp = "no"\n', encoding="utf-8")

        profile_disabled = _run(root, scons, platform, "profile_no", (f"profile={profile}",))
        _require(profile_disabled.returncode == 0, "MCP-disabled profile dry run failed.", profile_disabled)
        _require('target "template_debug"' in profile_disabled.output, "The profile target was not applied.", profile_disabled)
        _require(RUNTIME_SOURCE not in profile_disabled.output, "The disabled profile scheduled an MCP runtime source.", profile_disabled)

        command_line_enabled = _run(root, scons, platform, "cli_yes", (f"profile={profile}", "mcp=yes"))
        _require(command_line_enabled.returncode == 0, "Command-line MCP override dry run failed.", command_line_enabled)
        _require(RUNTIME_SOURCE in command_line_enabled.output, "mcp=yes did not override the disabled profile.", command_line_enabled)

        release = _run(root, scons, platform, "release", ("target=template_release", "mcp=yes"))
        _require(release.returncode != 0, "MCP-enabled release template was accepted.", release)
        _require(RELEASE_REJECTION in release.output, "Release rejection did not report the MCP policy.", release)

        gdscript_disabled = _run(
            root,
            scons,
            platform,
            "gdscript_no",
            (
                "target=editor",
                "mcp=yes",
                "module_gdscript_enabled=no",
                "module_jsonrpc_enabled=no",
                "module_websocket_enabled=no",
            ),
        )
        _require(gdscript_disabled.returncode == 0, "GDScript-disabled Editor dry run failed.", gdscript_disabled)
        _require(
            GDSCRIPT_SEMANTIC_SOURCE not in gdscript_disabled.output,
            "The GDScript-disabled Editor scheduled the MCP semantic service.",
            gdscript_disabled,
        )


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Exercise embedded MCP SCons option and source-selection contracts.")
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent.parent,
        help="Godot repository root.",
    )
    parser.add_argument("--scons", default="scons", help="SCons executable.")
    parser.add_argument("--platform", default=_platform_name(), help="Godot SCons platform.")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    try:
        run_checks(args.root.resolve(), args.scons, args.platform)
    except (OSError, RuntimeError) as error:
        print(f"MCP SCons config smoke failed: {error}", file=sys.stderr)
        return 1
    print("MCP SCons config smoke passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
