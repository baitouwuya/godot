/**************************************************************************/
/*  mcp_gdextension_tool_utils.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "mcp_gdextension_tool_utils.h"

#include "mcp_tool_utils.h"

namespace {

static constexpr int MIN_TIMEOUT_MS = 1000;
static constexpr int MAX_TIMEOUT_MS = 600000;
static constexpr int MAX_DIAGNOSTICS = 128;
static constexpr int MAX_OUTPUT_TAIL_LENGTH = 16384;

Dictionary _extension_path_schema(const String &p_description) {
	Dictionary schema = MCPToolUtils::make_property_schema("string", p_description);
	schema["pattern"] = "^res://.+\\.gdextension$";
	return schema;
}

Dictionary _profile_schema(const String &p_description, bool p_include_default = false) {
	Dictionary schema = MCPToolUtils::make_property_schema("string", p_description);
	schema["enum"] = PackedStringArray{ "debug", "release" };
	if (p_include_default) {
		schema["default"] = "debug";
	}
	return schema;
}

Dictionary _job_id_schema() {
	Dictionary schema = MCPToolUtils::make_property_schema("string", "Opaque GDExtension build job ID.");
	schema["minLength"] = 1;
	schema["maxLength"] = 128;
	return schema;
}

Dictionary _sha256_schema(const String &p_description) {
	Dictionary schema = MCPToolUtils::make_property_schema("string", p_description);
	schema["pattern"] = "^[0-9a-fA-F]{64}$";
	return schema;
}

Dictionary _non_negative_integer_schema(const String &p_description) {
	Dictionary schema = MCPToolUtils::make_property_schema("integer", p_description);
	schema["minimum"] = 0;
	return schema;
}

Dictionary _diagnostic_schema() {
	Dictionary severity = MCPToolUtils::make_property_schema("string", "Compiler diagnostic severity.");
	severity["enum"] = PackedStringArray{ "error", "warning", "note", "info" };
	Dictionary properties;
	properties["severity"] = severity;
	properties["code"] = MCPToolUtils::make_property_schema("string", "Compiler diagnostic code, when available.");
	properties["message"] = MCPToolUtils::make_property_schema("string", "Compiler diagnostic message.");
	properties["file"] = MCPToolUtils::make_property_schema("string", "Project-local source path, when available.");
	properties["line"] = _non_negative_integer_schema("One-based source line, or zero when unavailable.");
	properties["column"] = _non_negative_integer_schema("One-based source column, or zero when unavailable.");
	return MCPToolUtils::make_object_schema(properties,
			PackedStringArray{ "severity", "code", "message", "file", "line", "column" });
}

Dictionary _failure_schema() {
	Dictionary properties;
	properties["code"] = MCPToolUtils::make_property_schema("string", "Stable GDExtension build failure code.");
	properties["message"] = MCPToolUtils::make_property_schema("string", "Human-readable GDExtension build failure message.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "code", "message" });
}

Dictionary _job_output_schema() {
	Dictionary state = MCPToolUtils::make_property_schema("string", "GDExtension build job lifecycle state.");
	state["enum"] = PackedStringArray{ "queued", "running", "cancelling", "succeeded", "failed", "cancelled", "needs_restart" };

	Dictionary exit_code = MCPToolUtils::make_property_schema("integer", "Builder process exit code.");
	exit_code["minimum"] = -1;
	Dictionary stdout_tail = MCPToolUtils::make_property_schema("string", "Bounded trailing builder standard output.");
	stdout_tail["maxLength"] = MAX_OUTPUT_TAIL_LENGTH;
	Dictionary stderr_tail = MCPToolUtils::make_property_schema("string", "Bounded trailing builder standard error.");
	stderr_tail["maxLength"] = MAX_OUTPUT_TAIL_LENGTH;
	Dictionary diagnostics = MCPToolUtils::make_property_schema("array", "Bounded structured compiler diagnostics.");
	diagnostics["items"] = _diagnostic_schema();
	diagnostics["maxItems"] = MAX_DIAGNOSTICS;

	Dictionary properties;
	properties["jobId"] = _job_id_schema();
	properties["state"] = state;
	properties["extensionPath"] = _extension_path_schema("Project-local GDExtension configuration path.");
	properties["profile"] = _profile_schema("Builder profile selected for the job.");
	properties["libraryPath"] = MCPToolUtils::make_property_schema("string", "Resolved project-local GDExtension library path.");
	properties["exitCode"] = exit_code;
	properties["startedAtMs"] = _non_negative_integer_schema("Unix timestamp in milliseconds when the job started.");
	properties["finishedAtMs"] = _non_negative_integer_schema("Unix timestamp in milliseconds when the job reached a terminal state.");
	properties["projectRevision"] = _sha256_schema("SHA-256 project revision used by the build.");
	properties["artifactSha256"] = _sha256_schema("SHA-256 checksum of the installed GDExtension library.");
	properties["reloadStatus"] = MCPToolUtils::make_property_schema("string", "GDExtension reload or runtime restart outcome.");
	properties["diagnostics"] = diagnostics;
	properties["stdoutTail"] = stdout_tail;
	properties["stderrTail"] = stderr_tail;
	properties["rollbackComplete"] = MCPToolUtils::make_property_schema("boolean", "Whether a failed artifact replacement completed its rollback.");
	properties["failure"] = _failure_schema();
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "jobId", "state", "extensionPath", "profile" });
}

} // namespace

namespace MCPGDExtensionToolUtils {

Dictionary build_schema() {
	Dictionary clean = MCPToolUtils::make_property_schema("boolean", "Remove prior builder output before starting the build.");
	clean["default"] = false;
	Dictionary reload = MCPToolUtils::make_property_schema("string", "Post-build extension reload policy.");
	reload["enum"] = PackedStringArray{ "none", "if_reloadable", "restart_runtime" };
	reload["default"] = "if_reloadable";
	Dictionary timeout = MCPToolUtils::make_property_schema("integer", "Maximum build duration in milliseconds.");
	timeout["minimum"] = MIN_TIMEOUT_MS;
	timeout["maximum"] = MAX_TIMEOUT_MS;
	timeout["default"] = 120000;

	Dictionary properties;
	properties["extensionPath"] = _extension_path_schema("Project-local .gdextension path.");
	properties["profile"] = _profile_schema("Registered builder profile.", true);
	properties["clean"] = clean;
	properties["reload"] = reload;
	properties["timeoutMs"] = timeout;
	properties["expectedProjectRevision"] = _sha256_schema("Optional SHA-256 project revision required before building.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "extensionPath" });
}

Dictionary build_status_schema() {
	Dictionary properties;
	properties["jobId"] = _job_id_schema();
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "jobId" });
}

Dictionary build_cancel_schema() {
	Dictionary properties;
	properties["jobId"] = _job_id_schema();
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "jobId" });
}

Dictionary build_output_schema() {
	return _job_output_schema();
}

Dictionary build_status_output_schema() {
	return _job_output_schema();
}

Dictionary build_cancel_output_schema() {
	return _job_output_schema();
}

} // namespace MCPGDExtensionToolUtils
