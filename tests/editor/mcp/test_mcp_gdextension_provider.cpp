/**************************************************************************/
/*  test_mcp_gdextension_provider.cpp                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/*                                                                        */
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

#include "core/templates/local_vector.h"
#include "editor/mcp/providers/mcp_gdextension_build_service.h"
#include "editor/mcp/providers/mcp_gdextension_profile_registry.h"
#include "editor/mcp/providers/mcp_gdextension_tool_utils.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_gdextension_provider);

namespace TestMCPGDExtensionProvider {

static String _error_code(const Dictionary &p_result) {
	const Dictionary structured_content = p_result.get("structuredContent", Dictionary());
	return Dictionary(structured_content.get("error", Dictionary())).get("code", String());
}

static String _error_message(const Dictionary &p_result) {
	const Dictionary structured_content = p_result.get("structuredContent", Dictionary());
	return Dictionary(structured_content.get("error", Dictionary())).get("message", String());
}

TEST_CASE("[MCP][Provider] GDExtension tools expose bounded closed schemas") {
	CHECK(MCPGDExtensionProfileRegistry::names() == PackedStringArray{ "debug", "release" });
	MCPGDExtensionProfileRegistry::Profile selected_profile;
	CHECK(MCPGDExtensionProfileRegistry::resolve("debug", selected_profile));
	CHECK(selected_profile.scons_target == "template_debug");
	CHECK_FALSE(selected_profile.release);
	CHECK(MCPGDExtensionProfileRegistry::resolve("release", selected_profile));
	CHECK(selected_profile.scons_target == "template_release");
	CHECK(selected_profile.release);
	String profile_error;
	CHECK_FALSE(MCPGDExtensionProfileRegistry::resolve("asan", selected_profile, &profile_error));
	CHECK(profile_error.contains("debug"));
	CHECK_FALSE(MCPGDExtensionProfileRegistry::current_platform().is_empty());

	const Dictionary build = MCPGDExtensionToolUtils::build_schema();
	CHECK(build.get("type", String()) == "object");
	CHECK_FALSE(bool(build.get("additionalProperties", true)));

	const Dictionary build_properties = build.get("properties", Dictionary());
	CHECK(build_properties.has("extensionPath"));
	CHECK(build_properties.has("profile"));
	CHECK(build_properties.has("clean"));
	CHECK(build_properties.has("reload"));
	CHECK(build_properties.has("timeoutMs"));
	CHECK(build_properties.has("expectedProjectRevision"));
	const PackedStringArray required = build.get("required", PackedStringArray());
	CHECK(required.has("extensionPath"));
	const Dictionary extension_path = build_properties.get("extensionPath", Dictionary());
	CHECK(extension_path.get("type", String()) == "string");
	CHECK(extension_path.get("pattern", String()) == "^res://.+\\.gdextension$");
	const Dictionary profile = build_properties.get("profile", Dictionary());
	const PackedStringArray profile_values = profile.get("enum", PackedStringArray());
	REQUIRE(profile_values.size() == 2);
	CHECK(profile_values[0] == "debug");
	CHECK(profile_values[1] == "release");
	CHECK(profile.get("default", String()) == "debug");
	const Dictionary reload = build_properties.get("reload", Dictionary());
	CHECK(PackedStringArray(reload.get("enum", PackedStringArray())).has("none"));
	CHECK(PackedStringArray(reload.get("enum", PackedStringArray())).has("if_reloadable"));
	CHECK(PackedStringArray(reload.get("enum", PackedStringArray())).has("restart_runtime"));
	const Dictionary timeout = build_properties.get("timeoutMs", Dictionary());
	CHECK(int(timeout.get("minimum", 0)) == 1000);
	CHECK(int(timeout.get("maximum", 0)) == 600000);

	for (const Dictionary &schema : LocalVector<Dictionary>{
				 MCPGDExtensionToolUtils::build_status_schema(), MCPGDExtensionToolUtils::build_cancel_schema() }) {
		CHECK(schema.get("type", String()) == "object");
		CHECK_FALSE(bool(schema.get("additionalProperties", true)));
		CHECK(PackedStringArray(schema.get("required", PackedStringArray())).has("jobId"));
	}

	for (const Dictionary &output : LocalVector<Dictionary>{
				 MCPGDExtensionToolUtils::build_output_schema(), MCPGDExtensionToolUtils::build_status_output_schema(),
				 MCPGDExtensionToolUtils::build_cancel_output_schema() }) {
		CHECK(output.get("type", String()) == "object");
		CHECK_FALSE(bool(output.get("additionalProperties", true)));
		const Dictionary output_properties = output.get("properties", Dictionary());
		for (const String &name : PackedStringArray{
					 "jobId", "state", "extensionPath", "profile", "libraryPath", "exitCode", "startedAtMs", "finishedAtMs",
					 "projectRevision", "artifactSha256", "reloadStatus", "diagnostics", "stdoutTail", "stderrTail", "rollbackComplete", "failure" }) {
			CHECK(output_properties.has(name));
		}
		const Dictionary output_profile = output_properties.get("profile", Dictionary());
		CHECK(PackedStringArray(output_profile.get("enum", PackedStringArray())) == PackedStringArray{ "debug", "release" });
		const Dictionary diagnostics = output_properties.get("diagnostics", Dictionary());
		CHECK(int(diagnostics.get("maxItems", 0)) > 0);
		const Dictionary diagnostic = diagnostics.get("items", Dictionary());
		CHECK_FALSE(bool(diagnostic.get("additionalProperties", true)));
		for (const String &name : PackedStringArray{ "severity", "code", "message", "file", "line", "column" }) {
			CHECK(Dictionary(diagnostic.get("properties", Dictionary())).has(name));
		}
		const Dictionary failure = output_properties.get("failure", Dictionary());
		CHECK(failure.get("type", String()) == "object");
		CHECK_FALSE(bool(failure.get("additionalProperties", true)));
		const PackedStringArray failure_required = failure.get("required", PackedStringArray());
		CHECK(failure_required.size() == 2);
		CHECK(failure_required.has("code"));
		CHECK(failure_required.has("message"));
		const Dictionary failure_properties = failure.get("properties", Dictionary());
		REQUIRE(failure_properties.size() == 2);
		CHECK(Dictionary(failure_properties.get("code", Dictionary())).get("type", String()) == "string");
		CHECK(Dictionary(failure_properties.get("message", Dictionary())).get("type", String()) == "string");
	}
}

TEST_CASE("[MCP][Provider] GDExtension build rejects invalid profiles, paths, and sessions without launching a builder") {
	MCPGDExtensionBuildService service;

	Dictionary arguments;
	arguments["extensionPath"] = "res://extension.gdextension";
	for (const String &profile : PackedStringArray{ "", "asan", "Debug", "template_debug" }) {
		arguments["profile"] = profile;
		const Dictionary result = service.build(arguments, Dictionary());
		CHECK(bool(result.get("isError", false)));
		CHECK(_error_code(result) == "INVALID_ARGUMENTS");
		CHECK(_error_message(result).contains("profile"));
		CHECK(_error_message(result).contains("debug"));
		CHECK(_error_message(result).contains("release"));
	}

	for (const String &profile : PackedStringArray{ "debug", "release" }) {
		arguments["profile"] = profile;
		arguments["extensionPath"] = "res://extension.txt";
		const Dictionary result = service.build(arguments, Dictionary());
		CHECK(bool(result.get("isError", false)));
		CHECK(_error_code(result) == "INVALID_ARGUMENTS");
		CHECK(_error_message(result).contains("extensionPath must be a project-local .gdextension path"));
	}

	arguments["extensionPath"] = "res://extension.gdextension";
	arguments["profile"] = "debug";
	Dictionary invalid_session_context;
	invalid_session_context["session"] = "not-an-object";
	Dictionary result = service.build(arguments, invalid_session_context);
	CHECK(bool(result.get("isError", false)));
	CHECK(_error_code(result) == "INVALID_SESSION");

	Dictionary job_arguments;
	job_arguments["jobId"] = "missing-job";
	result = service.get_status(job_arguments, invalid_session_context);
	CHECK(bool(result.get("isError", false)));
	CHECK(_error_code(result) == "INVALID_SESSION");
	result = service.cancel(job_arguments, invalid_session_context);
	CHECK(bool(result.get("isError", false)));
	CHECK(_error_code(result) == "INVALID_SESSION");

	Dictionary empty_session_context;
	empty_session_context["session"] = Dictionary{ { "sessionId", "" } };
	result = service.get_status(job_arguments, empty_session_context);
	CHECK(bool(result.get("isError", false)));
	CHECK(_error_code(result) == "INVALID_SESSION");

	Dictionary valid_session_context;
	valid_session_context["session"] = Dictionary{ { "sessionId", "session-a" } };
	result = service.get_status(job_arguments, valid_session_context);
	CHECK(bool(result.get("isError", false)));
	CHECK(_error_code(result) == "BUILD_JOB_NOT_FOUND");
}

} // namespace TestMCPGDExtensionProvider
