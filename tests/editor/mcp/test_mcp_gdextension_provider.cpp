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
#include "editor/mcp/providers/mcp_gdextension_tool_utils.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_gdextension_provider);

namespace TestMCPGDExtensionProvider {

TEST_CASE("[MCP][Provider] GDExtension tools expose bounded closed schemas") {
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
	const Dictionary profile = build_properties.get("profile", Dictionary());
	CHECK(PackedStringArray(profile.get("enum", PackedStringArray())).has("debug"));
	CHECK(PackedStringArray(profile.get("enum", PackedStringArray())).has("release"));
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

	const Dictionary output = MCPGDExtensionToolUtils::build_output_schema();
	CHECK(output.get("type", String()) == "object");
	CHECK_FALSE(bool(output.get("additionalProperties", true)));
	const Dictionary output_properties = output.get("properties", Dictionary());
	for (const String &name : PackedStringArray{
				 "jobId", "state", "extensionPath", "profile", "libraryPath", "exitCode", "startedAtMs", "finishedAtMs",
				 "projectRevision", "artifactSha256", "reloadStatus", "diagnostics", "stdoutTail", "stderrTail", "rollbackComplete" }) {
		CHECK(output_properties.has(name));
	}
	const Dictionary diagnostics = output_properties.get("diagnostics", Dictionary());
	CHECK(int(diagnostics.get("maxItems", 0)) > 0);
	const Dictionary diagnostic = diagnostics.get("items", Dictionary());
	CHECK_FALSE(bool(diagnostic.get("additionalProperties", true)));
	for (const String &name : PackedStringArray{ "severity", "code", "message", "file", "line", "column" }) {
		CHECK(Dictionary(diagnostic.get("properties", Dictionary())).has(name));
	}
}

} // namespace TestMCPGDExtensionProvider
