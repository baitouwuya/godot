#!/usr/bin/env python3

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, List, Mapping, Optional, Sequence


class SmokeFailure(AssertionError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SmokeFailure(f"MCP CLI smoke assertion failed: {message}")


@dataclass(frozen=True)
class CommandResult:
    returncode: int
    stdout: str
    stderr: str
    arguments: Sequence[str]


def describe_command_result(result: CommandResult) -> str:
    return (
        f"command={list(result.arguments)!r}, returncode={result.returncode}, "
        f"stdout={result.stdout!r}, stderr={result.stderr!r}"
    )


def _creation_flags() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def _read_log(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


class HostProcess:
    def __init__(
        self,
        executable: Path,
        project_path: Path,
        label: str,
        working_directory: Path,
    ) -> None:
        self.project_path = project_path
        self.label = label
        self.stop_marker = project_path / ".mcp-smoke-stop"
        log_stem = re.sub(r"[^A-Za-z0-9_.-]+", "-", label).strip("-") or "host"
        self.stdout_path = working_directory / f".{log_stem}.stdout.log"
        self.stderr_path = working_directory / f".{log_stem}.stderr.log"
        self._stdout_writer = self.stdout_path.open("wb")
        self._stderr_writer = self.stderr_path.open("wb")
        self.arguments = [
            str(executable),
            "--editor",
            "--headless",
            "--path",
            str(project_path),
            "--mcp",
            "--mcp-port",
            "0",
        ]
        try:
            self.process = subprocess.Popen(
                self.arguments,
                cwd=str(working_directory),
                stdin=subprocess.DEVNULL,
                stdout=self._stdout_writer,
                stderr=self._stderr_writer,
                creationflags=_creation_flags(),
            )
        except Exception:
            self._close_logs()
            raise
        self._stopped = False

    def _close_logs(self) -> None:
        if not self._stdout_writer.closed:
            self._stdout_writer.close()
        if not self._stderr_writer.closed:
            self._stderr_writer.close()

    def result(self) -> CommandResult:
        return CommandResult(
            returncode=self.process.returncode if self.process.returncode is not None else -1,
            stdout=_read_log(self.stdout_path),
            stderr=_read_log(self.stderr_path),
            arguments=self.arguments,
        )

    def require_running(self) -> None:
        if self.process.poll() is None:
            return
        self._close_logs()
        result = self.result()
        raise SmokeFailure(
            f"{self.label} Host exited early with {result.returncode}. "
            f"stdout: {result.stdout!r} stderr: {result.stderr!r}"
        )

    def stop(self, grace_seconds: int = 8, require_graceful: bool = False) -> CommandResult:
        if self._stopped:
            return self.result()

        graceful = self.process.poll() is not None
        try:
            if self.process.poll() is None:
                self.stop_marker.write_text("stop", encoding="utf-8")
                try:
                    self.process.wait(timeout=grace_seconds)
                    graceful = True
                except subprocess.TimeoutExpired:
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        self.process.kill()
                        self.process.wait(timeout=2)
            else:
                self.process.wait(timeout=1)
        finally:
            self.stop_marker.unlink(missing_ok=True)
            self._close_logs()
            self._stopped = True

        result = self.result()
        if require_graceful:
            require(graceful, f"{self.label} Host ignored its stop marker.")
            require(result.returncode == 0, f"{self.label} Host exited with {result.returncode}: {result.stderr}")
            require(not self.stop_marker.exists(), f"{self.label} stop marker was not cleaned up.")
        return result


class ProcessRunner:
    def __init__(self, binary: Path, working_directory: Path, timeout_seconds: int) -> None:
        self.binary = binary
        self.host_binary = self._find_host_binary(binary)
        self.working_directory = working_directory
        self.timeout_seconds = timeout_seconds

    @staticmethod
    def resolve_binary(value: str) -> Path:
        candidate = Path(value).expanduser()
        if not candidate.is_file():
            resolved = shutil.which(value)
            require(resolved is not None, f"Godot binary was not found: {value}")
            candidate = Path(resolved)
        return candidate.resolve()

    @staticmethod
    def _find_host_binary(binary: Path) -> Path:
        suffix = ".console.exe"
        if str(binary).lower().endswith(suffix):
            editor_binary = Path(str(binary)[: -len(suffix)] + ".exe")
            if editor_binary.is_file():
                return editor_binary
        return binary

    def invoke(
        self,
        arguments: Sequence[str],
        input_lines: Optional[Iterable[str]] = None,
        executable: Optional[Path] = None,
        timeout_seconds: Optional[int] = None,
    ) -> CommandResult:
        command = [str(executable or self.binary), *arguments]
        input_text = None
        if input_lines is not None:
            input_text = "".join(f"{line}\n" for line in input_lines)
        try:
            completed = subprocess.run(
                command,
                cwd=str(self.working_directory),
                input=input_text,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout_seconds or self.timeout_seconds,
                creationflags=_creation_flags(),
                check=False,
            )
        except subprocess.TimeoutExpired as error:
            raise SmokeFailure(f"Godot command timed out: {' '.join(command)}") from error
        return CommandResult(
            returncode=completed.returncode,
            stdout=completed.stdout,
            stderr=completed.stderr,
            arguments=command,
        )

    def start_host(self, project_path: Path, label: str) -> HostProcess:
        return HostProcess(self.host_binary, project_path, label, self.working_directory)


def non_empty_lines(text: str) -> List[str]:
    return [line for line in text.splitlines() if line.strip()]


def load_tool_manifest(path: Path) -> List[str]:
    require(path.is_file(), f"Tool manifest does not exist: {path}")
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SmokeFailure(f"Tool manifest is not valid JSON: {path}: {error}") from error
    require(isinstance(manifest, dict), "Tool manifest root must be an object.")
    require(manifest.get("schemaVersion") == 1, "Tool manifest schemaVersion must be 1.")
    tools = manifest.get("tools")
    require(isinstance(tools, list) and tools, "Tool manifest must contain a non-empty tools array.")
    require(all(isinstance(name, str) and name.strip() for name in tools), "Tool manifest contains an invalid name.")
    require(len(set(tools)) == len(tools), "Tool manifest contains duplicate tool names.")
    return tools


def parse_discovery(result: CommandResult, label: str) -> Dict[str, Any]:
    require(result.returncode == 0, f"{label} discovery failed: {result.stderr}")
    lines = non_empty_lines(result.stdout)
    require(len(lines) == 1, f"{label} discovery stdout must contain exactly one JSON line: {result.stdout!r}")
    try:
        record = json.loads(lines[0])
    except json.JSONDecodeError as error:
        raise SmokeFailure(f"{label} discovery stdout is not valid JSON: {lines[0]!r}") from error
    require(isinstance(record, dict), f"{label} discovery record must be an object.")
    expected_fields = {"endpoint", "instanceId", "pid", "projectId", "protocolVersion"}
    require(set(record) == expected_fields, f"{label} discovery exposed unexpected fields: {sorted(record)}")
    require(type(record["pid"]) is int and record["pid"] > 0, f"{label} discovery pid must be a positive JSON integer.")
    require(isinstance(record["projectId"], str) and record["projectId"], f"{label} discovery has no projectId.")
    require(isinstance(record["instanceId"], str) and record["instanceId"], f"{label} discovery has no instanceId.")
    require(
        re.fullmatch(r"http://(?:127\.0\.0\.1|localhost|\[::1\]):[0-9]+/mcp", record["endpoint"]) is not None,
        f"{label} discovery endpoint is not a loopback /mcp URL: {record['endpoint']!r}",
    )
    return record


def json_line(value: Mapping[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=True, separators=(",", ":"))


def request(request_id: int, method: str, params: Mapping[str, Any]) -> Dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "method": method, "params": dict(params)}


def notification(method: str, params: Mapping[str, Any]) -> Dict[str, Any]:
    return {"jsonrpc": "2.0", "method": method, "params": dict(params)}


def tool_call(request_id: int, name: str, arguments: Mapping[str, Any]) -> Dict[str, Any]:
    return request(request_id, "tools/call", {"name": name, "arguments": dict(arguments)})


def parse_jsonrpc_responses(
    result: CommandResult,
    expected_ids: Iterable[int],
    label: str,
    request_descriptions: Optional[Sequence[str]] = None,
) -> Dict[int, Dict[str, Any]]:
    command_result = describe_command_result(result)
    require(result.returncode == 0, f"{label} failed: {command_result}")
    expected = set(expected_ids)
    lines = non_empty_lines(result.stdout)
    require(
        len(lines) == len(expected),
        f"{label} produced {len(lines)} responses instead of {len(expected)}: {command_result}",
    )
    responses: Dict[int, Dict[str, Any]] = {}
    for response_index, line in enumerate(lines):
        request_description = (
            request_descriptions[response_index]
            if request_descriptions is not None and response_index < len(request_descriptions)
            else "unknown request"
        )
        try:
            response = json.loads(line)
        except json.JSONDecodeError as error:
            raise SmokeFailure(
                f"{label} stdout response {response_index + 1} for {request_description} "
                f"is not JSON: {line!r}; {command_result}"
            ) from error
        require(isinstance(response, dict), f"{label} emitted a non-object JSON-RPC response.")
        require(response.get("jsonrpc") == "2.0", f"{label} emitted a response with an invalid jsonrpc field.")
        response_id = response.get("id")
        # Godot's Variant JSON bridge may encode an integer JSON number as a
        # floating-point value (for example, ``1.0``). Preserve the protocol
        # requirement that IDs are integral while normalizing that encoding.
        if type(response_id) is int:
            normalized_id = response_id
        elif type(response_id) is float and response_id.is_integer():
            normalized_id = int(response_id)
        else:
            normalized_id = None
        require(
            normalized_id is not None,
            f"{label} response {response_index + 1} for {request_description} has no integer id: "
            f"{response!r}; {command_result}",
        )
        require(normalized_id not in responses, f"{label} emitted duplicate response id {normalized_id}.")
        responses[normalized_id] = response
    require(set(responses) == expected, f"{label} response ids differ: {sorted(responses)} != {sorted(expected)}")
    return responses


def invoke_stdio(
    runner: ProcessRunner,
    project_path: Path,
    protocol_version: str,
    requests: Sequence[Mapping[str, Any]],
    label: str,
) -> Dict[int, Dict[str, Any]]:
    initialize = request(
        1,
        "initialize",
        {
            "protocolVersion": protocol_version,
            "capabilities": {},
            "clientInfo": {"name": "godot-mcp-python-smoke", "version": "1"},
        },
    )
    initialized = notification("notifications/initialized", {})
    expected_ids = [1]
    request_descriptions = ["initialize (id=1)"]
    for item in requests:
        request_id = item.get("id")
        require(type(request_id) is int and request_id != 1, f"{label} contains an invalid or reserved request id.")
        expected_ids.append(request_id)
        method = item.get("method", "<missing method>")
        params = item.get("params")
        tool_name = params.get("name") if isinstance(params, dict) else None
        suffix = f", tool={tool_name}" if isinstance(tool_name, str) else ""
        request_descriptions.append(f"{method} (id={request_id}{suffix})")
    result = runner.invoke(
        ["--verbose", "--mcp-stdio", "--path", str(project_path)],
        [json_line(initialize), json_line(initialized), *(json_line(item) for item in requests)],
    )
    return parse_jsonrpc_responses(result, expected_ids, label, request_descriptions)


def wait_until(deadline_seconds: int, operation: Any, description: str, interval: float = 0.2) -> Any:
    deadline = time.monotonic() + deadline_seconds
    last_value: Any = None
    while time.monotonic() < deadline:
        last_value = operation()
        if last_value:
            return last_value
        time.sleep(interval)
    raise SmokeFailure(f"Timed out waiting for {description}; last value: {last_value!r}")


def remove_tree_with_retry(path: Path) -> None:
    for attempt in range(20):
        try:
            shutil.rmtree(path)
            return
        except FileNotFoundError:
            return
        except OSError:
            if attempt == 19:
                raise
            time.sleep(0.25)
