#Requires -Version 7.2

[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[ValidateNotNullOrEmpty()]
	[string]$Binary,

	[ValidateRange(5, 300)]
	[int]$TimeoutSeconds = 45,

	[switch]$KeepTemporaryProjects
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-Condition {
	param(
		[Parameter(Mandatory = $true)]
		[bool]$Condition,

		[Parameter(Mandatory = $true)]
		[string]$Message
	)

	if (-not $Condition) {
		throw "MCP CLI smoke assertion failed: $Message"
	}
}

function Get-NonEmptyLines {
	param([AllowEmptyString()][string]$Text)

	return @($Text -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Test-ToolResultError {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Result
	)

	$property = $Result.PSObject.Properties["isError"]
	return $null -ne $property -and [bool]$property.Value
}

function Start-CapturedGodot {
	param(
		[string]$Executable = $script:BinaryPath,

		[Parameter(Mandatory = $true)]
		[string[]]$Arguments,

		[AllowNull()]
		[string[]]$InputLines = $null
	)

	$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
	$startInfo.FileName = $Executable
	$startInfo.WorkingDirectory = $script:TemporaryRoot
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	$startInfo.RedirectStandardOutput = $true
	$startInfo.RedirectStandardError = $true
	$startInfo.RedirectStandardInput = $null -ne $InputLines
	foreach ($argument in $Arguments) {
		[void]$startInfo.ArgumentList.Add($argument)
	}

	$process = [System.Diagnostics.Process]::new()
	$process.StartInfo = $startInfo
	if (-not $process.Start()) {
		throw "Unable to start Godot binary '$Executable'."
	}

	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	if ($null -ne $InputLines) {
		try {
			foreach ($line in $InputLines) {
				$process.StandardInput.WriteLine($line)
			}
		} finally {
			$process.StandardInput.Close()
		}
	}

	return [pscustomobject]@{
		Process = $process
		StdoutTask = $stdoutTask
		StderrTask = $stderrTask
		Arguments = $Arguments
	}
}

function Wait-CapturedGodot {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Capture,

		[Parameter(Mandatory = $true)]
		[int]$Timeout
	)

	if (-not $Capture.Process.WaitForExit($Timeout * 1000)) {
		throw "Godot process timed out after $Timeout seconds: $($Capture.Arguments -join ' ')"
	}
	$Capture.Process.WaitForExit()
	return [pscustomobject]@{
		ExitCode = $Capture.Process.ExitCode
		Stdout = $Capture.StdoutTask.GetAwaiter().GetResult()
		Stderr = $Capture.StderrTask.GetAwaiter().GetResult()
	}
}

function Stop-CapturedGodot {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Capture,

		[AllowNull()]
		[string]$StopMarker = $null,

		[int]$GraceSeconds = 8
	)

	$process = $Capture.Process
	if (-not [string]::IsNullOrEmpty($StopMarker)) {
		try {
			Set-Content -LiteralPath $StopMarker -Value "stop" -NoNewline -Encoding utf8NoBOM
			if (-not $process.HasExited) {
				[void]$process.WaitForExit($GraceSeconds * 1000)
			}
		} catch {
			Write-Warning "Unable to request graceful Godot shutdown through '$StopMarker': $_"
		}
	}

	if (-not $process.HasExited) {
		try {
			if ($process.CloseMainWindow()) {
				[void]$process.WaitForExit(2000)
			}
		} catch {
			# A headless process has no window to close.
		}
	}

	if (-not $process.HasExited) {
		Write-Warning "Godot process $($process.Id) did not exit gracefully; terminating it."
		try {
			$process.Kill($true)
		} catch [System.InvalidOperationException] {
			if (-not $process.HasExited) {
				throw
			}
		}
	}
	$process.WaitForExit()
	[void]$Capture.StdoutTask.GetAwaiter().GetResult()
	[void]$Capture.StderrTask.GetAwaiter().GetResult()

	if (-not [string]::IsNullOrEmpty($StopMarker) -and (Test-Path -LiteralPath $StopMarker)) {
		Remove-Item -LiteralPath $StopMarker -Force
	}
}

function Invoke-Godot {
	param(
		[string]$Executable = $script:BinaryPath,

		[Parameter(Mandatory = $true)]
		[string[]]$Arguments,

		[AllowNull()]
		[string[]]$InputLines = $null
	)

	$capture = Start-CapturedGodot -Executable $Executable -Arguments $Arguments -InputLines $InputLines
	try {
		return Wait-CapturedGodot -Capture $capture -Timeout $script:TimeoutSeconds
	} finally {
		if (-not $capture.Process.HasExited) {
			Stop-CapturedGodot -Capture $capture
		}
		$capture.Process.Dispose()
	}
}

function New-SmokeProject {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Path,

		[Parameter(Mandatory = $true)]
		[string]$Name
	)

	$addonPath = Join-Path $Path "addons/mcp_smoke_cleanup"
	[void](New-Item -ItemType Directory -Path $addonPath -Force)

	$projectSettings = @"
; Engine configuration file.
config_version=5

[application]
config/name="$Name"
run/main_scene="res://main.tscn"

[editor_plugins]
enabled=PackedStringArray("res://addons/mcp_smoke_cleanup/plugin.cfg")
"@
	Set-Content -LiteralPath (Join-Path $Path "project.godot") -Value $projectSettings -Encoding utf8NoBOM

	$mainScene = @'
[gd_scene format=3]

[node name="Root" type="Node"]
'@
	Set-Content -LiteralPath (Join-Path $Path "main.tscn") -Value $mainScene -Encoding utf8NoBOM

	$nodeScript = @'
extends Node
@export var target: NodePath
'@
	Set-Content -LiteralPath (Join-Path $Path "node_script.gd") -Value $nodeScript -Encoding utf8NoBOM

	$childScene = @'
[gd_scene format=3]

[node name="ChildScene" type="Node"]

[node name="Nested" type="Node" parent="."]
'@
	Set-Content -LiteralPath (Join-Path $Path "child_scene.tscn") -Value $childScene -Encoding utf8NoBOM

	$pluginConfig = @"
[plugin]
name="MCP Smoke Cleanup"
description="Stops the temporary editor process during MCP CLI smoke cleanup."
author="Godot"
version="1.0"
script="plugin.gd"
"@
	Set-Content -LiteralPath (Join-Path $addonPath "plugin.cfg") -Value $pluginConfig -Encoding utf8NoBOM

	$pluginScript = @'
@tool
extends EditorPlugin

const STOP_FILE := "res://.mcp-smoke-stop"
const SCENE_READY_FILE := "res://.mcp-smoke-scene-ready"

func _enter_tree() -> void:
	set_process(true)

func _process(_delta: float) -> void:
	if FileAccess.file_exists(STOP_FILE):
		get_tree().quit()
		return
	if EditorInterface.get_edited_scene_root() == null:
		EditorInterface.open_scene_from_path("res://main.tscn")
	elif not FileAccess.file_exists(SCENE_READY_FILE):
		var ready_file := FileAccess.open(SCENE_READY_FILE, FileAccess.WRITE)
		if ready_file:
			ready_file.store_string("ready")
'@
	Set-Content -LiteralPath (Join-Path $addonPath "plugin.gd") -Value $pluginScript -Encoding utf8NoBOM
}

function Convert-DiscoveryResult {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Result,

		[Parameter(Mandatory = $true)]
		[string]$Label
	)

	Assert-Condition ($Result.ExitCode -eq 0) "$Label discovery failed: $($Result.Stderr)"
	$lines = @(Get-NonEmptyLines -Text $Result.Stdout)
	Assert-Condition ($lines.Count -eq 1) "$Label discovery stdout must contain exactly one JSON line."
	try {
		$record = $lines[0] | ConvertFrom-Json -ErrorAction Stop
	} catch {
		throw "$Label discovery stdout is not valid JSON: $($lines[0])"
	}

	$expectedFields = @("endpoint", "instanceId", "pid", "projectId", "protocolVersion")
	$actualFields = @($record.PSObject.Properties.Name | Sort-Object)
	Assert-Condition (($actualFields -join ",") -ceq ($expectedFields -join ",")) "$Label discovery exposed unexpected fields: $($actualFields -join ', ')"
	Assert-Condition (-not [string]::IsNullOrWhiteSpace([string]$record.projectId)) "$Label discovery has no project ID."
	Assert-Condition (-not [string]::IsNullOrWhiteSpace([string]$record.instanceId)) "$Label discovery has no instance ID."
	Assert-Condition ([string]$record.endpoint -match '^http://(127\.0\.0\.1|localhost|\[::1\]):[0-9]+/mcp$') "$Label discovery endpoint is not a loopback /mcp URL."
	return $record
}

function Convert-JsonRpcResponseMap {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Result,

		[Parameter(Mandatory = $true)]
		[int]$ExpectedCount,

		[Parameter(Mandatory = $true)]
		[string]$Label
	)

	Assert-Condition ($Result.ExitCode -eq 0) "$Label failed: $($Result.Stderr)"
	$lines = @(Get-NonEmptyLines -Text $Result.Stdout)
	Assert-Condition ($lines.Count -eq $ExpectedCount) "$Label produced $($lines.Count) stdout responses instead of $ExpectedCount."
	$responsesById = @{}
	foreach ($line in $lines) {
		try {
			$response = $line | ConvertFrom-Json -ErrorAction Stop
		} catch {
			throw "$Label stdout contains a non-JSON line: $line"
		}
		Assert-Condition ([string]$response.jsonrpc -ceq "2.0") "$Label stdout contains JSON that is not JSON-RPC."
		Assert-Condition ($null -ne $response.id) "$Label emitted a JSON-RPC response without an id."
		$id = [string]$response.id
		Assert-Condition (-not $responsesById.ContainsKey($id)) "$Label emitted duplicate response id $id."
		$responsesById[$id] = $response
	}
	return $responsesById
}

function New-JsonRpcRequest {
	param(
		[Parameter(Mandatory = $true)]
		[int]$Id,

		[Parameter(Mandatory = $true)]
		[string]$Method,

		[Parameter(Mandatory = $true)]
		[hashtable]$Params
	)

	return @{
		jsonrpc = "2.0"
		id = $Id
		method = $Method
		params = $Params
	} | ConvertTo-Json -Depth 12 -Compress
}

function New-ToolCallRequest {
	param(
		[Parameter(Mandatory = $true)]
		[int]$Id,

		[Parameter(Mandatory = $true)]
		[string]$Name,

		[Parameter(Mandatory = $true)]
		[hashtable]$Arguments
	)

	return New-JsonRpcRequest -Id $Id -Method "tools/call" -Params @{
		name = $Name
		arguments = $Arguments
	}
}

function Get-TextSha256 {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Text
	)

	$bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($Text)
	return [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
}

function Wait-ForDiscovery {
	param(
		[Parameter(Mandatory = $true)]
		[object]$McpHost,

		[Parameter(Mandatory = $true)]
		[string]$Label
	)

	$deadline = [DateTime]::UtcNow.AddSeconds($script:TimeoutSeconds)
	$lastResult = $null
	while ([DateTime]::UtcNow -lt $deadline) {
		if ($McpHost.Capture.Process.HasExited) {
			$hostResult = Wait-CapturedGodot -Capture $McpHost.Capture -Timeout 1
			throw "$Label Host exited before discovery. Exit $($hostResult.ExitCode). stderr: $($hostResult.Stderr)"
		}

		$lastResult = Invoke-Godot -Arguments @("--verbose", "--mcp-discover", "--path", $McpHost.ProjectPath)
		if ($lastResult.ExitCode -eq 0) {
			return Convert-DiscoveryResult -Result $lastResult -Label $Label
		}
		Start-Sleep -Milliseconds 250
	}

	$lastError = if ($null -eq $lastResult) { "no discovery attempt completed" } else { $lastResult.Stderr }
	throw "Timed out waiting for $Label MCP discovery: $lastError"
}

function Wait-ForHostFile {
	param(
		[Parameter(Mandatory = $true)]
		[object]$McpHost,

		[Parameter(Mandatory = $true)]
		[string]$RelativePath,

		[Parameter(Mandatory = $true)]
		[string]$Label
	)

	$targetPath = Join-Path $McpHost.ProjectPath $RelativePath
	$deadline = [DateTime]::UtcNow.AddSeconds($script:TimeoutSeconds)
	while ([DateTime]::UtcNow -lt $deadline) {
		if (Test-Path -LiteralPath $targetPath -PathType Leaf) {
			return
		}
		if ($McpHost.Capture.Process.HasExited) {
			$hostResult = Wait-CapturedGodot -Capture $McpHost.Capture -Timeout 1
			throw "$Label Host exited before '$RelativePath' was ready. Exit $($hostResult.ExitCode). stderr: $($hostResult.Stderr)"
		}
		Start-Sleep -Milliseconds 100
	}
	throw "Timed out waiting for $Label Host file '$RelativePath'."
}

function Start-McpHost {
	param(
		[Parameter(Mandatory = $true)]
		[string]$ProjectPath,

		[Parameter(Mandatory = $true)]
		[string]$Label
	)

	$capture = Start-CapturedGodot -Executable $script:HostBinaryPath -Arguments @("--editor", "--headless", "--path", $ProjectPath, "--mcp", "--mcp-port", "0")
	$hostEntry = [pscustomobject]@{
		Capture = $capture
		ProjectPath = $ProjectPath
		StopMarker = Join-Path $ProjectPath ".mcp-smoke-stop"
		Label = $Label
	}
	[void]$script:Hosts.Add($hostEntry)
	return $hostEntry
}

$binaryCommand = Get-Command -Name $Binary -CommandType Application -ErrorAction Stop | Select-Object -First 1
$script:BinaryPath = $binaryCommand.Source
$script:HostBinaryPath = $script:BinaryPath
if ($script:BinaryPath.EndsWith(".console.exe", [System.StringComparison]::OrdinalIgnoreCase)) {
	$editorBinary = $script:BinaryPath.Substring(0, $script:BinaryPath.Length - ".console.exe".Length) + ".exe"
	if (Test-Path -LiteralPath $editorBinary -PathType Leaf) {
		$script:HostBinaryPath = $editorBinary
	}
}
$script:TimeoutSeconds = $TimeoutSeconds
$script:TemporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("godot-mcp-smoke-" + [Guid]::NewGuid().ToString("N"))
$script:Hosts = [System.Collections.Generic.List[object]]::new()

[void](New-Item -ItemType Directory -Path $script:TemporaryRoot -Force)
$projectA = Join-Path $script:TemporaryRoot "project-a"
$projectB = Join-Path $script:TemporaryRoot "project-b"
New-SmokeProject -Path $projectA -Name "MCP Smoke A"
New-SmokeProject -Path $projectB -Name "MCP Smoke B"

try {
	Write-Host "[1/9] Checking explicit project path and CLI/editor conflicts"
	$missingPath = Invoke-Godot -Arguments @("--mcp-discover")
	Assert-Condition ($missingPath.ExitCode -ne 0) "--mcp-discover without --path must fail."
	Assert-Condition (@(Get-NonEmptyLines -Text $missingPath.Stdout).Count -eq 0) "missing-path diagnostics must not use stdout."
	Assert-Condition ($missingPath.Stderr -match 'requires? an explicit --path') "missing-path diagnostic is not actionable."

	$modeConflict = Invoke-Godot -Arguments @("--editor", "--mcp-discover", "--path", $projectA)
	Assert-Condition ($modeConflict.ExitCode -ne 0) "CLI commands must conflict with editor mode."
	Assert-Condition ($modeConflict.Stderr -match 'cannot be combined with editor or project manager mode') "CLI/editor conflict diagnostic is missing."

	Write-Host "[2/9] Checking ordinary editor startup remains MCP-free"
	$ordinary = Invoke-Godot -Arguments @("--editor", "--headless", "--path", $projectA, "--quit")
	Assert-Condition ($ordinary.ExitCode -eq 0) "ordinary headless editor startup failed: $($ordinary.Stderr)"
	Assert-Condition ("$($ordinary.Stdout)`n$($ordinary.Stderr)" -notmatch 'MCP Host started') "ordinary editor startup unexpectedly enabled MCP."
	$ordinarySceneMarker = Join-Path $projectA ".mcp-smoke-scene-ready"
	if (Test-Path -LiteralPath $ordinarySceneMarker -PathType Leaf) {
		Remove-Item -LiteralPath $ordinarySceneMarker -Force
	}

	Write-Host "[3/9] Starting project A Host and validating public discovery"
	$hostA = Start-McpHost -ProjectPath $projectA -Label "project A"
	$discoveryA = Wait-ForDiscovery -McpHost $hostA -Label "project A"
	Assert-Condition ([int64]$discoveryA.pid -gt 0) "project A discovery PID is invalid."

	Write-Host "[4/9] Starting project B Host and validating project routing"
	$hostB = Start-McpHost -ProjectPath $projectB -Label "project B"
	$discoveryB = Wait-ForDiscovery -McpHost $hostB -Label "project B"
	Assert-Condition ([int64]$discoveryB.pid -gt 0) "project B discovery PID is invalid."
	Assert-Condition ([int64]$discoveryA.pid -ne [int64]$discoveryB.pid) "two simultaneous Hosts published the same PID."
	Assert-Condition ([string]$discoveryA.projectId -cne [string]$discoveryB.projectId) "two project paths resolved to the same project ID."
	Assert-Condition ([string]$discoveryA.instanceId -cne [string]$discoveryB.instanceId) "two Hosts reused an instance ID."
	Assert-Condition ([string]$discoveryA.endpoint -cne [string]$discoveryB.endpoint) "two simultaneous Hosts published the same endpoint."

	Write-Host "[5/9] Checking same-project Host conflict"
	$conflict = Invoke-Godot -Executable $script:HostBinaryPath -Arguments @("--editor", "--headless", "--path", $projectA, "--mcp", "--mcp-port", "0")
	Assert-Condition ($conflict.ExitCode -ne 0) "a second Host for project A must fail. Exit $($conflict.ExitCode). stdout: $($conflict.Stdout) stderr: $($conflict.Stderr)"
	Assert-Condition ("$($conflict.Stdout)`n$($conflict.Stderr)" -match 'Another MCP Host already owns this project|MCP project lease is owned') "same-project Host conflict diagnostic is missing."
	Assert-Condition (-not $hostA.Capture.Process.HasExited) "project A owner exited during the conflict check."
	$discoveryAAfterConflict = Wait-ForDiscovery -McpHost $hostA -Label "project A after conflict"
	Assert-Condition ([string]$discoveryAAfterConflict.instanceId -ceq [string]$discoveryA.instanceId) "same-project conflict replaced the owning discovery record."

	Write-Host "[6/9] Checking stdio stdout and the complete tool surface"
	$initializeRequest = @{
		jsonrpc = "2.0"
		id = 1
		method = "initialize"
		params = @{
			protocolVersion = [string]$discoveryA.protocolVersion
			capabilities = @{}
			clientInfo = @{
				name = "godot-mcp-smoke"
				version = "1"
			}
		}
	} | ConvertTo-Json -Depth 6 -Compress
	$initializedNotification = @{
		jsonrpc = "2.0"
		method = "notifications/initialized"
		params = @{}
	} | ConvertTo-Json -Depth 4 -Compress
	$toolsListRequest = @{
		jsonrpc = "2.0"
		id = 2
		method = "tools/list"
		params = @{}
	} | ConvertTo-Json -Depth 4 -Compress
	$initialScriptText = "## Smoke script.`nextends Node`n`n## Stored value.`nvar value: int = 1`n`nfunc get_value() -> int:`n`treturn value`n"
	$initialScriptSha = Get-TextSha256 -Text $initialScriptText
	$createScriptRequest = New-ToolCallRequest -Id 3 -Name "godot.script.create" -Arguments @{
		path = "res://mcp_smoke_script.gd"
		text = $initialScriptText
	}
	$stdio = Invoke-Godot -Arguments @("--verbose", "--mcp-stdio", "--path", $projectA) -InputLines @(
		$initializeRequest,
		$initializedNotification,
		$toolsListRequest,
		$createScriptRequest
	)
	$responsesById = Convert-JsonRpcResponseMap -Result $stdio -ExpectedCount 3 -Label "stdio initialize/tools-list/script-create"
	Assert-Condition ($responsesById.ContainsKey("1")) "stdio initialize response is missing."
	Assert-Condition ($responsesById.ContainsKey("2")) "stdio tools/list response is missing."
	Assert-Condition ($responsesById.ContainsKey("3")) "stdio script/create response is missing."

	$expectedTools = @(
		"godot.editor.get_state",
		"godot.editor.undo",
		"godot.editor.redo",
		"godot.file.create",
		"godot.resource.import_options",
		"godot.resource.import",
		"godot.scene.get_tree",
		"godot.scene.get_selection",
		"godot.scene.save",
		"godot.node.get_properties",
		"godot.node.create",
		"godot.node.set_property",
		"godot.node.attach_script",
		"godot.node.delete",
		"godot.node.rename",
		"godot.node.reparent",
		"godot.node.move",
		"godot.node.duplicate",
		"godot.node.instantiate_scene",
		"godot.script.create",
		"godot.script.get",
		"godot.script.usages",
		"godot.script.edit",
		"godot.script.save",
		"godot.gdscript.diagnostics",
		"godot.gdscript.symbols",
		"godot.gdscript.completion",
		"godot.gdscript.hover",
		"godot.gdscript.definition",
		"godot.gdscript.declaration",
		"godot.gdscript.references",
		"godot.gdscript.signature_help",
		"godot.gdscript.rename"
	) | Sort-Object
	$actualTools = @($responsesById["2"].result.tools | ForEach-Object { [string]$_.name } | Sort-Object)
	Assert-Condition ($actualTools.Count -eq 33) "tools/list returned $($actualTools.Count) tools instead of 33."
	Assert-Condition (($actualTools -join "`n") -ceq ($expectedTools -join "`n")) "tools/list did not expose the expected 33-tool surface."
	$createResult = $responsesById["3"].result.structuredContent
	Assert-Condition (-not (Test-ToolResultError -Result $responsesById["3"].result)) "script/create returned a tool error."
	Assert-Condition ([string]$createResult.sha256 -ceq $initialScriptSha) "script/create returned an unexpected SHA-256."
	Assert-Condition (-not [bool]$createResult.unsaved) "a newly created script must initially match disk."
	$createProjectMeta = $responsesById["3"].result._meta.'io.godot/project'
	Assert-Condition ([string]$createProjectMeta.projectId -ceq [string]$discoveryA.projectId) "tool result projectId does not match project A discovery."
	Assert-Condition ([string]$createProjectMeta.instanceId -ceq [string]$discoveryA.instanceId) "tool result instanceId does not match project A discovery."

	Write-Host "[7/9] Checking dirty ScriptEditor authority, stale edits, diagnostics, and save"
	$dirtyScriptText = "extends Node`nvar value: int =`n"
	$dirtyScriptSha = Get-TextSha256 -Text $dirtyScriptText
	$scriptFlow = Invoke-Godot -Arguments @("--verbose", "--mcp-stdio", "--path", $projectA) -InputLines @(
		$initializeRequest,
		$initializedNotification,
		(New-ToolCallRequest -Id 11 -Name "godot.script.get" -Arguments @{ path = "res://mcp_smoke_script.gd" }),
		(New-ToolCallRequest -Id 17 -Name "godot.script.usages" -Arguments @{
			path = "res://mcp_smoke_script.gd"
			member = @{ kind = "property"; name = "value" }
		}),
		(New-ToolCallRequest -Id 12 -Name "godot.script.edit" -Arguments @{
			path = "res://mcp_smoke_script.gd"
			text = $dirtyScriptText
			expected_revision = [int64]$createResult.revision
		}),
		(New-ToolCallRequest -Id 13 -Name "godot.gdscript.diagnostics" -Arguments @{ path = "res://mcp_smoke_script.gd" }),
		(New-ToolCallRequest -Id 14 -Name "godot.script.edit" -Arguments @{
			path = "res://mcp_smoke_script.gd"
			text = $initialScriptText
			expected_sha256 = $initialScriptSha
		}),
		(New-ToolCallRequest -Id 15 -Name "godot.script.save" -Arguments @{ path = "res://mcp_smoke_script.gd" }),
		(New-ToolCallRequest -Id 16 -Name "godot.gdscript.diagnostics" -Arguments @{ path = "res://mcp_smoke_script.gd" })
	)
	$scriptResponses = Convert-JsonRpcResponseMap -Result $scriptFlow -ExpectedCount 8 -Label "stdio dirty-script flow"
	foreach ($responseId in @("1", "11", "12", "13", "14", "15", "16", "17")) {
		Assert-Condition ($scriptResponses.ContainsKey($responseId)) "dirty-script flow response $responseId is missing."
	}

	$getResult = $scriptResponses["11"].result.structuredContent
	Assert-Condition ([string]$getResult.sha256 -ceq $initialScriptSha) "a second MCP session did not read the authoritative initial ScriptEditor buffer."
	Assert-Condition ([string]$getResult.view -ceq "documentation") "script/get did not default to the documentation view."
	Assert-Condition (-not ($getResult.PSObject.Properties.Name -contains "lines")) "documentation script/get returned a source lines array."
	Assert-Condition (-not ($getResult.PSObject.Properties.Name -contains "lineBase")) "documentation script/get returned obsolete line-base metadata."
	Assert-Condition (-not ($getResult.PSObject.Properties.Name -contains "memberCount")) "documentation script/get returned a redundant member count."
	Assert-Condition (-not ($getResult.PSObject.Properties.Name -contains "members")) "documentation script/get returned an ungrouped members array."
	Assert-Condition ([string]$getResult.documentation -ceq "Smoke script.") "documentation script/get did not return parsed class documentation."
	Assert-Condition (@($getResult.properties).Count -eq 1) "documentation script/get returned an unexpected property count."
	Assert-Condition ([string]$getResult.properties[0].text -ceq "var value: int = 1") "documentation script/get returned an unexpected property declaration."
	Assert-Condition ([int]$getResult.properties[0].line -eq 5) "documentation script/get did not return a 1-based source line."
	$editResult = $scriptResponses["12"].result.structuredContent
	Assert-Condition (-not (Test-ToolResultError -Result $scriptResponses["12"].result)) "script/edit returned a tool error."
	Assert-Condition ([string]$editResult.sha256 -ceq $dirtyScriptSha) "script/edit returned an unexpected dirty SHA-256."
	Assert-Condition ([bool]$editResult.unsaved) "script/edit did not leave the ScriptEditor buffer unsaved."
	Assert-Condition (@($editResult.diagnostics).Count -gt 0) "script/edit did not return immediate diagnostics for invalid text."

	$dirtyDiagnostics = $scriptResponses["13"].result.structuredContent
	Assert-Condition ([string]$dirtyDiagnostics.sha256 -ceq $dirtyScriptSha) "GDScript diagnostics read stale disk text instead of the dirty ScriptEditor buffer."
	Assert-Condition ([int64]$dirtyDiagnostics.revision -eq [int64]$editResult.revision) "script/edit and diagnostics did not report the same revision."
	Assert-Condition (@($dirtyDiagnostics.diagnostics).Count -gt 0) "dirty GDScript diagnostics unexpectedly returned no errors."

	$staleResult = $scriptResponses["14"].result
	Assert-Condition (Test-ToolResultError -Result $staleResult) "script/edit accepted a stale SHA-256."
	Assert-Condition ([string]$staleResult.structuredContent.error.code -ceq "stale_revision") "stale script/edit returned the wrong error code."

	$saveResult = $scriptResponses["15"].result.structuredContent
	Assert-Condition (-not (Test-ToolResultError -Result $scriptResponses["15"].result)) "script/save returned a tool error."
	Assert-Condition ([bool]$saveResult.saved) "script/save did not report success."
	Assert-Condition (-not [bool]$saveResult.unsaved) "script/save left the ScriptEditor buffer unsaved."
	$storedScriptText = [System.IO.File]::ReadAllText((Join-Path $projectA "mcp_smoke_script.gd"), [System.Text.UTF8Encoding]::new($false))
	Assert-Condition ($storedScriptText -ceq [string]$saveResult.text) "script/save result and disk text differ."
	Assert-Condition ((Get-TextSha256 -Text $storedScriptText) -ceq [string]$saveResult.sha256) "script/save result and disk SHA-256 differ."
	$afterSaveDiagnostics = $scriptResponses["16"].result.structuredContent
	Assert-Condition ([string]$afterSaveDiagnostics.sha256 -ceq [string]$saveResult.sha256) "saved diagnostics and ScriptEditor SHA-256 differ."
	Assert-Condition ([int64]$afterSaveDiagnostics.revision -eq [int64]$saveResult.revision) "saved diagnostics and ScriptEditor revision differ."
	$usageResult = $scriptResponses["17"].result.structuredContent
	Assert-Condition (-not (Test-ToolResultError -Result $scriptResponses["17"].result)) "script/usages returned a tool error."
	Assert-Condition ([string]$usageResult.member.kind -ceq "property") "script/usages returned the wrong member kind."
	Assert-Condition ([string]$usageResult.member.name -ceq "value") "script/usages returned the wrong member name."
	Assert-Condition ([int]$usageResult.count -eq 1) "script/usages returned $($usageResult.count) semantic property usages instead of 1: $($usageResult.locations | ConvertTo-Json -Depth 10 -Compress)"
	Assert-Condition ([int]$usageResult.locations[0].range.start.line -eq 7) "script/usages returned the wrong zero-based usage line."

	Write-Host "[8/9] Checking Node undo/redo, script attachment, and explicit scene save"
	Wait-ForHostFile -McpHost $hostA -RelativePath ".mcp-smoke-scene-ready" -Label "project A scene"
	$nodeFlow = Invoke-Godot -Arguments @("--verbose", "--mcp-stdio", "--path", $projectA) -InputLines @(
		$initializeRequest,
		$initializedNotification,
		(New-ToolCallRequest -Id 21 -Name "godot.node.create" -Arguments @{ type = "Node"; name = "McpChild"; parentPath = "." }),
		(New-ToolCallRequest -Id 22 -Name "godot.scene.get_tree" -Arguments @{ maxDepth = 3 }),
		(New-ToolCallRequest -Id 23 -Name "godot.editor.get_state" -Arguments @{}),
		(New-ToolCallRequest -Id 24 -Name "godot.editor.undo" -Arguments @{}),
		(New-ToolCallRequest -Id 25 -Name "godot.scene.get_tree" -Arguments @{}),
		(New-ToolCallRequest -Id 26 -Name "godot.editor.redo" -Arguments @{}),
		(New-ToolCallRequest -Id 27 -Name "godot.node.set_property" -Arguments @{ path = "McpChild"; property = "process_mode"; value = "i:3" }),
		(New-ToolCallRequest -Id 28 -Name "godot.node.get_properties" -Arguments @{ path = "McpChild" }),
		(New-ToolCallRequest -Id 29 -Name "godot.editor.undo" -Arguments @{}),
		(New-ToolCallRequest -Id 30 -Name "godot.node.get_properties" -Arguments @{ path = "McpChild" }),
		(New-ToolCallRequest -Id 31 -Name "godot.editor.redo" -Arguments @{}),
		(New-ToolCallRequest -Id 32 -Name "godot.node.attach_script" -Arguments @{ path = "McpChild"; scriptPath = "res://node_script.gd" }),
		(New-ToolCallRequest -Id 33 -Name "godot.node.get_properties" -Arguments @{ path = "McpChild" }),
		(New-ToolCallRequest -Id 34 -Name "godot.editor.undo" -Arguments @{}),
		(New-ToolCallRequest -Id 35 -Name "godot.node.get_properties" -Arguments @{ path = "McpChild" }),
		(New-ToolCallRequest -Id 36 -Name "godot.editor.redo" -Arguments @{}),
		(New-ToolCallRequest -Id 37 -Name "godot.node.attach_script" -Arguments @{ path = "."; scriptPath = "res://node_script.gd" }),
		(New-ToolCallRequest -Id 38 -Name "godot.node.set_property" -Arguments @{ path = "."; property = "target"; value = "np:McpChild" }),
		(New-ToolCallRequest -Id 39 -Name "godot.node.rename" -Arguments @{ path = "McpChild"; name = "McpRenamed" }),
		(New-ToolCallRequest -Id 40 -Name "godot.node.get_properties" -Arguments @{ path = "." }),
		(New-ToolCallRequest -Id 41 -Name "godot.node.duplicate" -Arguments @{ path = "McpRenamed"; name = "McpClone" }),
		(New-ToolCallRequest -Id 42 -Name "godot.node.set_property" -Arguments @{ path = "."; property = "target"; value = "np:McpClone" }),
		(New-ToolCallRequest -Id 43 -Name "godot.node.create" -Arguments @{ type = "Node"; name = "Container"; parentPath = "." }),
		(New-ToolCallRequest -Id 44 -Name "godot.node.reparent" -Arguments @{ path = "McpClone"; parentPath = "Container"; index = 0 }),
		(New-ToolCallRequest -Id 45 -Name "godot.node.get_properties" -Arguments @{ path = "." }),
		(New-ToolCallRequest -Id 46 -Name "godot.node.create" -Arguments @{ type = "Node"; name = "Sibling"; parentPath = "Container" }),
		(New-ToolCallRequest -Id 47 -Name "godot.node.move" -Arguments @{ path = "Container/McpClone"; index = -1 }),
		(New-ToolCallRequest -Id 48 -Name "godot.node.instantiate_scene" -Arguments @{ scenePath = "res://child_scene.tscn"; parentPath = "Container"; name = "Instanced"; index = 0 }),
		(New-ToolCallRequest -Id 49 -Name "godot.node.delete" -Arguments @{ path = "Container/McpClone" }),
		(New-ToolCallRequest -Id 50 -Name "godot.node.get_properties" -Arguments @{ path = "." }),
		(New-ToolCallRequest -Id 51 -Name "godot.editor.undo" -Arguments @{}),
		(New-ToolCallRequest -Id 52 -Name "godot.node.get_properties" -Arguments @{ path = "." }),
		(New-ToolCallRequest -Id 53 -Name "godot.scene.get_tree" -Arguments @{ maxDepth = 3 }),
		(New-ToolCallRequest -Id 54 -Name "godot.editor.redo" -Arguments @{}),
		(New-ToolCallRequest -Id 55 -Name "godot.scene.save" -Arguments @{}),
		(New-ToolCallRequest -Id 56 -Name "godot.editor.get_state" -Arguments @{})
	)
	$nodeResponses = Convert-JsonRpcResponseMap -Result $nodeFlow -ExpectedCount 37 -Label "stdio node undo/redo flow"
	foreach ($responseId in @("1") + (21..56 | ForEach-Object { [string]$_ })) {
		Assert-Condition ($nodeResponses.ContainsKey($responseId)) "node flow response $responseId is missing."
		if ($responseId -ne "1") {
			$result = $nodeResponses[$responseId].result
			$resultJson = $result | ConvertTo-Json -Depth 12 -Compress
			Assert-Condition (-not (Test-ToolResultError -Result $result)) "node flow response $responseId returned a tool error: $resultJson"
		}
	}

	Assert-Condition ([string]$nodeResponses["21"].result.structuredContent.path -ceq "McpChild") "node/create returned the wrong path."
	$createdChildren = @($nodeResponses["22"].result.structuredContent.root.children | ForEach-Object { [string]$_.name })
	Assert-Condition ($createdChildren -ccontains "McpChild") "created node is missing from the scene tree."
	Assert-Condition (@($nodeResponses["23"].result.structuredContent.unsavedScenes) -ccontains "res://main.tscn") "node/create did not leave the scene unsaved."
	Assert-Condition ([bool]$nodeResponses["24"].result.structuredContent.performed) "editor/undo did not undo node creation."
	$undoneChildren = @($nodeResponses["25"].result.structuredContent.root.children | ForEach-Object { [string]$_.name })
	Assert-Condition ($undoneChildren -cnotcontains "McpChild") "undo left the created node in the scene tree."
	Assert-Condition ([bool]$nodeResponses["26"].result.structuredContent.performed) "editor/redo did not restore node creation."

	$setProperties = @($nodeResponses["28"].result.structuredContent.properties)
	$setProcessMode = @($setProperties | Where-Object { [string]$_.name -ceq "process_mode" })
	Assert-Condition ($setProcessMode.Count -eq 1 -and [string]$setProcessMode[0].value -ceq "i:3") "node/set_property did not set process_mode."
	$undoneProperties = @($nodeResponses["30"].result.structuredContent.properties)
	$undoneProcessMode = @($undoneProperties | Where-Object { [string]$_.name -ceq "process_mode" })
	Assert-Condition ($undoneProcessMode.Count -eq 1 -and [string]$undoneProcessMode[0].value -ceq "i:0") "undo did not restore process_mode."

	$attachedProperties = @($nodeResponses["33"].result.structuredContent.properties)
	$attachedScript = @($attachedProperties | Where-Object { [string]$_.name -ceq "script" })
	Assert-Condition ($attachedScript.Count -eq 1) "attached node did not expose its script property."
	$attachedScriptValue = $attachedScript[0].value
	$attachedScriptArgs = @($attachedScriptValue.args | ForEach-Object { [string]$_ })
	Assert-Condition ([string]$attachedScriptValue.type -ceq "Dictionary") "node/attach_script did not return a canonical Dictionary Variant."
	Assert-Condition ($attachedScriptArgs -contains "sn:__godot_mcp_resource_path__" -and $attachedScriptArgs -contains "s:res://node_script.gd") "node/attach_script attached the wrong resource."
	$detachedProperties = @($nodeResponses["35"].result.structuredContent.properties)
	$detachedScript = @($detachedProperties | Where-Object { [string]$_.name -ceq "script" })
	Assert-Condition ($detachedScript.Count -eq 1 -and $null -eq $detachedScript[0].value) "undo did not detach the node script."
	Assert-Condition ([string]$nodeResponses["39"].result.structuredContent.path -ceq "McpRenamed") "node/rename returned the wrong path."
	$renamedRootProperties = @($nodeResponses["40"].result.structuredContent.properties)
	$renamedTarget = @($renamedRootProperties | Where-Object { [string]$_.name -ceq "target" })
	Assert-Condition ($renamedTarget.Count -eq 1 -and [string]$renamedTarget[0].value -ceq "np:McpRenamed") "node/rename did not rewrite the exported NodePath."
	Assert-Condition ([string]$nodeResponses["41"].result.structuredContent.path -ceq "McpClone") "node/duplicate returned the wrong path."
	Assert-Condition ([string]$nodeResponses["44"].result.structuredContent.path -ceq "Container/McpClone") "node/reparent returned the wrong path."
	$reparentedRootProperties = @($nodeResponses["45"].result.structuredContent.properties)
	$reparentedTarget = @($reparentedRootProperties | Where-Object { [string]$_.name -ceq "target" })
	Assert-Condition ($reparentedTarget.Count -eq 1 -and [string]$reparentedTarget[0].value -ceq "np:Container/McpClone") "node/reparent did not rewrite the exported NodePath."
	Assert-Condition ([int]$nodeResponses["47"].result.structuredContent.index -eq 1) "node/move did not move the node to the final sibling index."
	Assert-Condition ([string]$nodeResponses["48"].result.structuredContent.sceneFilePath -ceq "res://child_scene.tscn") "node/instantiate_scene returned the wrong scene path."
	Assert-Condition ([string]$nodeResponses["49"].result.structuredContent.deleted.path -ceq "Container/McpClone") "node/delete returned the wrong deleted path."
	$deletedRootProperties = @($nodeResponses["50"].result.structuredContent.properties)
	$deletedTarget = @($deletedRootProperties | Where-Object { [string]$_.name -ceq "target" })
	Assert-Condition ($deletedTarget.Count -eq 1 -and [string]$deletedTarget[0].value -ceq "np:") "node/delete did not clear the exported NodePath."
	Assert-Condition ([bool]$nodeResponses["51"].result.structuredContent.performed) "editor/undo did not restore the deleted node."
	$restoredRootProperties = @($nodeResponses["52"].result.structuredContent.properties)
	$restoredTarget = @($restoredRootProperties | Where-Object { [string]$_.name -ceq "target" })
	Assert-Condition ($restoredTarget.Count -eq 1 -and [string]$restoredTarget[0].value -ceq "np:Container/McpClone") "undo did not restore the exported NodePath."
	$containerTree = @($nodeResponses["53"].result.structuredContent.root.children | Where-Object { [string]$_.name -ceq "Container" })
	Assert-Condition ($containerTree.Count -eq 1) "the structure flow did not create Container."
	$containerChildren = @($containerTree[0].children | ForEach-Object { [string]$_.name })
	Assert-Condition ($containerChildren -ccontains "McpClone" -and $containerChildren -ccontains "Sibling" -and $containerChildren -ccontains "Instanced") "undo did not restore the expected Container subtree."
	Assert-Condition ([bool]$nodeResponses["54"].result.structuredContent.performed) "editor/redo did not delete the restored node again."
	Assert-Condition ([bool]$nodeResponses["55"].result.structuredContent.saved) "scene/save did not report success."
	Assert-Condition (-not (@($nodeResponses["56"].result.structuredContent.unsavedScenes) -ccontains "res://main.tscn")) "scene/save left main.tscn unsaved."
	Assert-Condition (Test-Path -LiteralPath (Join-Path $projectA "main.tscn") -PathType Leaf) "scene/save did not preserve main.tscn on disk."

	Write-Host "[9/9] Rechecking project B after project A stdio traffic"
	$discoveryBAfterStdio = Wait-ForDiscovery -McpHost $hostB -Label "project B after stdio"
	Assert-Condition ([string]$discoveryBAfterStdio.instanceId -ceq [string]$discoveryB.instanceId) "project A stdio traffic changed project B routing."

	Write-Host "MCP CLI smoke test passed."
} finally {
	for ($index = $script:Hosts.Count - 1; $index -ge 0; $index--) {
		$hostEntry = $script:Hosts[$index]
		try {
			Stop-CapturedGodot -Capture $hostEntry.Capture -StopMarker $hostEntry.StopMarker
		} catch {
			Write-Warning "Failed to clean up $($hostEntry.Label) Host: $_"
		} finally {
			$hostEntry.Capture.Process.Dispose()
		}
	}

	if ($KeepTemporaryProjects) {
		Write-Host "Temporary projects kept at: $($script:TemporaryRoot)"
	} elseif (Test-Path -LiteralPath $script:TemporaryRoot) {
		$removed = $false
		for ($attempt = 0; $attempt -lt 20 -and -not $removed; $attempt++) {
			try {
				Remove-Item -LiteralPath $script:TemporaryRoot -Recurse -Force
				$removed = $true
			} catch {
				if ($attempt -eq 19) {
					Write-Warning "Unable to remove temporary projects at '$($script:TemporaryRoot)': $_"
				} else {
					Start-Sleep -Milliseconds 250
				}
			}
		}
	}
}
