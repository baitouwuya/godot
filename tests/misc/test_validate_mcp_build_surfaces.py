#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
VALIDATOR_PATH = REPOSITORY_ROOT / "misc/scripts/validate_mcp_build_surfaces.py"
SPEC = importlib.util.spec_from_file_location("validate_mcp_build_surfaces", VALIDATOR_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Unable to load {VALIDATOR_PATH}")
VALIDATOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VALIDATOR
SPEC.loader.exec_module(VALIDATOR)


class MCPBuildSurfaceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        for relative_path in VALIDATOR.BUILD_FILES:
            source = REPOSITORY_ROOT / relative_path
            destination = self.root / relative_path
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def replace(self, relative_path: str, old: str, new: str) -> None:
        path = self.root / relative_path
        content = path.read_text(encoding="utf-8")
        self.assertIn(old, content)
        path.write_text(content.replace(old, new, 1), encoding="utf-8")

    def test_repository_build_surfaces_pass(self) -> None:
        self.assertEqual(VALIDATOR.validate_mcp_build_surfaces(self.root), [])

    def test_mcp_option_must_remain_tristate(self) -> None:
        self.replace('SConstruct', '["auto", "no", "yes"]', '["no", "yes"]')

        violations = VALIDATOR.validate_mcp_build_surfaces(self.root)

        self.assertIn("mcp-option", [violation.gate for violation in violations])

    def test_release_template_rejection_is_required(self) -> None:
        self.replace(
            'SConstruct',
            'if env["target"] == "template_release" and env["mcp"]:\n'
            '    print_error("The embedded MCP development tools cannot be enabled in release templates.")\n'
            '    Exit(255)',
            'if env["target"] == "template_release" and env["mcp"]:\n'
            '    print_error("The embedded MCP development tools cannot be enabled in release templates.")\n'
            '    Exit(0)',
        )

        violations = VALIDATOR.validate_mcp_build_surfaces(self.root)

        self.assertIn("release-rejection", [violation.gate for violation in violations])

    def test_core_mcp_requires_both_editor_and_mcp_guards(self) -> None:
        self.replace('core/SCsub', 'if env.editor_build and env["mcp"]:', 'if env.editor_build:')

        violations = VALIDATOR.validate_mcp_build_surfaces(self.root)

        self.assertIn("core-editor-only", [violation.gate for violation in violations])

    def test_cli_and_runtime_prefix_filters_are_contracts(self) -> None:
        self.replace('main/SCsub', 'startswith("mcp_")', 'startswith("optional_")')
        self.replace('scene/debugger/SCsub', 'startswith("mcp_runtime_")', 'startswith("optional_runtime_")')

        violations = VALIDATOR.validate_mcp_build_surfaces(self.root)
        gates = [violation.gate for violation in violations]

        self.assertIn("cli-source-gate", gates)
        self.assertIn("runtime-source-gate", gates)

    def test_editor_mcp_tests_follow_the_product_surface(self) -> None:
        self.replace(
            'tests/SCsub',
            'editor_mcp_enabled = env.editor_build and env["mcp"]',
            'editor_mcp_enabled = env["mcp"]',
        )

        violations = VALIDATOR.validate_mcp_build_surfaces(self.root)

        self.assertIn("test-source-gate", [violation.gate for violation in violations])


if __name__ == "__main__":
    unittest.main()
