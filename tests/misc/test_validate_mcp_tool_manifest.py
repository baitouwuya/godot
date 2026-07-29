#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
VALIDATOR_PATH = REPOSITORY_ROOT / "misc/scripts/validate_mcp_tool_manifest.py"
SPEC = importlib.util.spec_from_file_location("validate_mcp_tool_manifest", VALIDATOR_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Unable to load {VALIDATOR_PATH}")
VALIDATOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VALIDATOR
SPEC.loader.exec_module(VALIDATOR)


class MCPToolManifestTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.manifest = self.root / "manifest.json"

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def write(self, value: object) -> None:
        self.manifest.write_text(json.dumps(value), encoding="utf-8")

    def validate(self, expected_count: int = 2) -> list[Any]:
        return VALIDATOR.validate_mcp_tool_manifest(self.root, self.manifest, expected_count)

    def test_repository_manifest_passes(self) -> None:
        self.assertEqual(VALIDATOR.validate_mcp_tool_manifest(REPOSITORY_ROOT), [])

    def test_valid_manifest_passes(self) -> None:
        self.write({"schemaVersion": 1, "tools": ["godot.test.first", "godot.test.second"]})

        self.assertEqual(self.validate(), [])

    def test_schema_fields_version_and_count_are_enforced(self) -> None:
        self.write({"schemaVersion": 2, "tools": ["godot.test.first"], "extra": True})

        rules = [violation.rule for violation in self.validate()]

        self.assertEqual(rules, ["fields", "schema-version", "tool-count"])

    def test_names_and_duplicates_have_stable_diagnostics(self) -> None:
        self.write(
            {
                "schemaVersion": 1,
                "tools": ["godot.test.first", "godot.test.first", "Godot invalid"],
            }
        )

        violations = self.validate(expected_count=3)

        self.assertEqual([violation.rule for violation in violations], ["duplicate-tool", "tool-name"])
        self.assertIn("godot.test.first", violations[0].reason)
        self.assertIn("tools[2]", violations[1].reason)

    def test_missing_and_invalid_json_are_reported(self) -> None:
        missing = VALIDATOR.validate_mcp_tool_manifest(self.root, self.root / "missing.json", 1)
        self.assertEqual([violation.rule for violation in missing], ["file"])

        self.manifest.write_text("{", encoding="utf-8")
        self.assertEqual([violation.rule for violation in self.validate()], ["json"])

    def test_cli_returns_failure_for_a_violation(self) -> None:
        self.write({"schemaVersion": 1, "tools": []})

        result = subprocess.run(
            [
                sys.executable,
                str(VALIDATOR_PATH),
                "--root",
                str(self.root),
                "--manifest",
                str(self.manifest),
                "--expected-tool-count",
                "2",
            ],
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("tool-count", result.stderr)
        self.assertIn("1 violation(s)", result.stderr)


if __name__ == "__main__":
    unittest.main()
