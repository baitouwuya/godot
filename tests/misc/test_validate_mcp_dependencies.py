#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
VALIDATOR_PATH = REPOSITORY_ROOT / "misc/scripts/validate_mcp_dependencies.py"
SPEC = importlib.util.spec_from_file_location("validate_mcp_dependencies", VALIDATOR_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Unable to load {VALIDATOR_PATH}")
VALIDATOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VALIDATOR
SPEC.loader.exec_module(VALIDATOR)


class MCPDependencyBoundaryTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        for directory in ("core/mcp", "editor/mcp/providers", "main", "scene/debugger"):
            (self.root / directory).mkdir(parents=True)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def write(self, path: str, content: str) -> None:
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")

    def test_valid_dependencies_and_provider_self_include_pass(self) -> None:
        self.write("core/mcp/mcp_protocol.cpp", '#include "core/mcp/mcp_protocol.h"\n')
        self.write("editor/mcp/mcp_host.cpp", '#include "core/mcp/mcp_protocol.h"\n')
        self.write("editor/mcp/providers/mcp_scene_provider.cpp", '#include "mcp_scene_provider.h"\n')
        self.write("main/mcp_cli.cpp", '#include "core/mcp/mcp_discovery.h"\n')
        self.write("scene/debugger/mcp_runtime_input.cpp", '#include "scene/main/scene_tree.h"\n')

        violations, source_count = VALIDATOR.validate_mcp_dependencies(self.root)

        self.assertEqual(violations, [])
        self.assertEqual(source_count, 5)

    def test_all_dependency_boundaries_report_stable_diagnostics(self) -> None:
        self.write("core/mcp/mcp_protocol.cpp", '\n#include "../../editor/editor_node.h"\n')
        self.write("editor/mcp/mcp_http_server.cpp", '#include "providers/mcp_scene_provider.h"\n')
        self.write("editor/mcp/providers/mcp_scene_provider.cpp", '#include "mcp_node_provider.h"\n')
        self.write("main/mcp_cli.cpp", '#include "editor/mcp/mcp_host.h"\n')
        self.write("scene/debugger/mcp_runtime_input.cpp", '#include "editor/debugger/editor_debugger_node.h"\n')

        violations, _ = VALIDATOR.validate_mcp_dependencies(self.root)

        self.assertEqual(
            [violation.rule for violation in violations],
            [
                "core-mcp-isolation",
                "transport-host-isolation",
                "provider-isolation",
                "cli-adapter-isolation",
                "runtime-editor-isolation",
            ],
        )
        self.assertEqual(violations[0].line, 2)
        self.assertEqual(violations[0].resolved_include, "editor/editor_node.h")
        self.assertIn("mcp_http_server.cpp:1", violations[1].format())

    def test_cli_adapter_may_use_non_mcp_editor_path_support(self) -> None:
        self.write("main/mcp_cli.cpp", '#include "editor/file_system/editor_paths.h"\n')

        violations, _ = VALIDATOR.validate_mcp_dependencies(self.root)

        self.assertEqual(violations, [])

    def test_cli_adapter_cannot_depend_on_runtime_bridge(self) -> None:
        self.write("main/mcp_cli_runtime.cpp", '#include "scene/debugger/mcp_runtime_input.h"\n')

        violations, _ = VALIDATOR.validate_mcp_dependencies(self.root)

        self.assertEqual([violation.rule for violation in violations], ["cli-adapter-isolation"])

    def test_non_provider_helpers_are_not_treated_as_providers(self) -> None:
        self.write("editor/mcp/providers/mcp_scene_service.cpp", '#include "mcp_scene_provider.h"\n')

        violations, _ = VALIDATOR.validate_mcp_dependencies(self.root)

        self.assertEqual(violations, [])

    def test_cli_returns_failure_for_a_violation(self) -> None:
        self.write("core/mcp/mcp_protocol.h", '#include "main/main.h"\n')

        result = subprocess.run(
            [sys.executable, str(VALIDATOR_PATH), "--root", str(self.root)],
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("core-mcp-isolation", result.stderr)
        self.assertIn("1 violation(s)", result.stderr)


if __name__ == "__main__":
    unittest.main()
